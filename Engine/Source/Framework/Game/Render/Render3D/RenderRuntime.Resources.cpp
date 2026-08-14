#include "RenderRuntime.h"

#include "AppServices/RuntimeServices.h"
#include "Render3D/Services/DebugRenderSystem.h"
#include "RHI/Backend/Vulkan/VulkanSwapChain.h"
#include "RHI/Core/RenderResourceFactory.h"
#include "RHI/Core/Swapchain.h"
#include "Render3D/Pipelines/BasicPostprocessing.h"
#include "Resource/AssetManager.h"
#include "Core/Common/DeferredDeletionQueue.h"
#include "GUI/Resources/FontManager.h"
#include "Resource/Mesh/PrimitiveMeshCache.h"
#include "Core/ResourceRegistry.h"
#include "RHI/Backend/TextureLibrary.h"

namespace ya
{

namespace
{

std::shared_ptr<RenderImage> createPresentationRenderImage(IRender& render, VulkanSwapChain& swapchain, uint32_t imageIndex)
{
    const auto& swapchainCI = swapchain.getCreateInfo();
    auto importedImage = render.getResourceFactory()->importImage(ImportedImageDesc{
        .label         = std::format("Presentation_{}", imageIndex),
        .nativeHandle  = static_cast<void*>(swapchain.getVkImages().at(imageIndex)),
        .format        = swapchain.getFormat(),
        .usage         = static_cast<EImageUsage::T>(EImageUsage::ColorAttachment |
                    (swapchainCI.bEnableTransferSrc ? EImageUsage::TransferSrc : EImageUsage::None)),
        .extent        = {.width = swapchain.getExtent().width, .height = swapchain.getExtent().height, .depth = 1},
        .initialLayout = EImageLayout::Undefined,
        .finalLayout   = EImageLayout::PresentSrcKHR,
    });
    YA_CORE_ASSERT(importedImage != nullptr, "Failed to import presentation image {}", imageIndex);

    auto imageView = render.getResourceFactory()->createImageView(
        importedImage,
        ImageViewCreateInfo{
            .label          = std::format("Presentation_{}_View", imageIndex),
            .viewType       = EImageViewType::View2D,
            .aspectFlags    = EImageAspect::Color,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        });
    YA_CORE_ASSERT(imageView != nullptr, "Failed to create presentation image view {}", imageIndex);

    auto renderImage      = std::make_shared<RenderImage>();
    renderImage->label    = std::format("Presentation_{}", imageIndex);
    renderImage->image    = std::move(importedImage);
    renderImage->defaultView = std::move(imageView);
    return renderImage;
}

} // namespace

DescriptorSetHandle RenderRuntime::getSceneSkyboxDescriptorSet(Scene* scene)
{
    return _sharedResourceProvider.getSceneSkyboxDescriptorSet(scene);
}

DescriptorSetHandle RenderRuntime::getSceneEnvironmentLightingDescriptorSet(Scene* scene)
{
    return _sharedResourceProvider.getSceneEnvironmentLightingDescriptorSet(scene);
}

EnvironmentLightingSceneResources RenderRuntime::resolveSceneEnvironmentLightingResources(Scene* scene) const
{
    return _sharedResourceProvider.resolveSceneEnvironmentLightingResources(scene);
}

void RenderRuntime::init(const InitDesc& desc)
{
    YA_PROFILE_FUNCTION_LOG();
    YA_CORE_ASSERT(desc.hostServices != nullptr, "RenderRuntime init requires host services");

    initRuntimeState(desc);
    initShaderSystems();
    initRenderBackend(desc);
    initDiagnostics(desc);
    initResourceCaches();
    initSharedRenderResources();
    initPresentationResources();
    initCommandResources();
    initFrameServices();
}

void RenderRuntime::initRuntimeState(const InitDesc& desc)
{
    _hostServices       = desc.hostServices;
    _offscreenScheduler = desc.offscreenScheduler;
    _environmentLightingProvider = desc.environmentLightingProvider;
    _activeSceneProvider         = desc.activeSceneProvider;

    currentRenderAPI = ERenderAPI::Vulkan;
    _viewportRect    = Rect2D{
                 .pos    = {0.0f, 0.0f},
                 .extent = {static_cast<float>(desc.windowWidth), static_cast<float>(desc.windowHeight)},
    };
}

void RenderRuntime::initShaderSystems()
{
    auto shaderProcessor = ShaderProcessorFactory()
                               .withProcessorType(ShaderProcessorFactory::EProcessorType::GLSL)
                               .withShaderStoragePath("Engine/Shader/GLSL")
                               .withCachedStoragePath("Engine/Intermediate/Shader/GLSL")
                               .FactoryNew<GLSLProcessor>();
    _shaderStorage = std::make_shared<ShaderStorage>(shaderProcessor);

    auto slangProcessor = ShaderProcessorFactory()
                              .withProcessorType(ShaderProcessorFactory::EProcessorType::Slang)
                              .withShaderStoragePath("Engine/Shader/Slang")
                              .withCachedStoragePath("Engine/Intermediate/Shader/Slang")
                              .FactoryNew<SlangProcessor>();
    _shaderStorage->setSlangProcessor(slangProcessor);

    _shaderStorage->preloadAsync({
        ShaderDesc{.shaderName = "Test/Unlit.glsl"},
        ShaderDesc{.shaderName = "Test/SimpleMaterial.glsl"},
        ShaderDesc{
            .sourceMode = ShaderDesc::ESourceMode::StageFiles,
            .stageFiles = {
                ShaderDesc::StageFile{.stage = EShaderStage::Vertex, .file = "Sprite2D.slang"},
                ShaderDesc::StageFile{.stage = EShaderStage::Fragment, .file = "Sprite2D.slang"},
            },
        },
        ShaderDesc{.shaderName = "Test/DebugRender.glsl"},
        ShaderDesc{.shaderName = "Test/DebugPrimitiveLine.glsl"},
        ShaderDesc{.shaderName = "Test/DebugPrimitiveShape.glsl"},
        ShaderDesc{.shaderName = "PostProcessing/Basic.glsl"},
        ShaderDesc{.shaderName = "Skybox.glsl"},
        ShaderDesc{.shaderName = "Shadow/DirectionalLightDepthBuffer.glsl"},
        ShaderDesc{.shaderName = "CombineShadowMappingGenerate.slang"},
        ShaderDesc{.shaderName = "Shadow/PointShadowCull.comp.slang"},
        ShaderDesc{.shaderName = "Shadow/PointShadowIndirect.slang"},
        ShaderDesc{.shaderName = "Misc/pbr_generate_brdf_lut.slang"},
    });
    _deleter.push("ShaderStorage", [this](void*)
                  { _shaderStorage.reset(); });
}

void RenderRuntime::initDiagnostics(const InitDesc& desc)
{
    _diagnostics.init(_render, desc.bEnableRenderDoc, desc.renderDocDllPath, desc.renderDocCaptureOutputDir);
    _deleter.push("RenderDiagnostics", [this](void*)
                  { _diagnostics.shutdown(); });
}

void RenderRuntime::initRenderBackend(const InitDesc& desc)
{
    YA_CORE_ASSERT(_hostServices != nullptr, "RenderRuntime requires host services before initializing render backend");
    auto* nativeWindow = _hostServices->getOrCreateMainNativeWindow(WindowCreateInfo{
        .index      = 0,
        .renderAPI  = currentRenderAPI,
        .title      = desc.windowTitle,
        .width      = desc.windowWidth,
        .height     = desc.windowHeight,
        .scale      = 1.0f,
        .bResizable = true,
    });
    YA_CORE_ASSERT(nativeWindow != nullptr, "Native window must exist before initializing render backend");

    RenderCreateInfo renderCI{
        .renderAPI   = currentRenderAPI,
        .swapchainCI = SwapchainCreateInfo{
            .imageFormat        = EFormat::R8G8B8A8_UNORM,
            .bVsync             = false,
            .minImageCount      = 3,
            // Presentation readback must be available even when the screenshot
            // is requested at runtime through the control port, not only when
            // a startup automation screenshot path was configured.
            .bEnableTransferSrc = true,
            .width              = desc.windowWidth,
            .height             = desc.windowHeight,
        },
        .disabledGraphicsCards = {},
    };

    renderCI.nativeWindow = nativeWindow;

    _render = IRender::create(renderCI);
    YA_CORE_ASSERT(_render, "Failed to create IRender instance");
    // The backend consumes the shader service while building pipelines; the
    // runtime injects it right after backend creation so RHI/backend stay free
    // of host-layer lookups (initShaderSystems runs before this point).
    _render->setShaderStorage(_shaderStorage);
    _render->init(renderCI);
}

void RenderRuntime::initResourceCaches()
{
    TextureLibrary::get().init(_render);
    AssetManager::get()->setRender(_render);
    PrimitiveMeshCache::get().setRender(_render);

    ResourceRegistry::get().registerCache(&PrimitiveMeshCache::get(), 100);
    ResourceRegistry::get().registerCache(&TextureLibrary::get(), 90);
    ResourceRegistry::get().registerCache(FontManager::get(), 80);
    ResourceRegistry::get().registerCache(AssetManager::get(), 70);
}

void RenderRuntime::initSharedRenderResources()
{
    _sharedResourceProvider.init(_render, _environmentLightingProvider, _activeSceneProvider);

    _deleter.push("RenderBindings", [this](void*)
                  { _sharedResourceProvider.shutdown(); });

    _shaderStorage->waitForPreload();
    _shaderStorage->validate(ShaderDesc{.shaderName = "PhongLit/PhongLit.glsl"});

    initActivePipeline();
}

void RenderRuntime::initPresentationResources()
{
    rebuildPresentationImages();

    _presentationPostProcessor = ya::makeShared<BasicPostprocessing>();
    _presentationPostProcessor->init(BasicPostprocessing::InitDesc{
        .render                = _render,
        .renderPass            = nullptr,
        .pipelineRenderingInfo = PipelineRenderingInfo{
            .label                   = "RuntimePresentation",
            .viewMask                = 0,
            .colorAttachmentFormats  = {_render->getSwapchain()->getFormat()},
            .depthAttachmentFormat   = EFormat::Undefined,
            .stencilAttachmentFormat = EFormat::Undefined,
        },
    });

    _render->getSwapchain()->onRecreate.addLambda(
        this,
        [this](ISwapchain::DiffInfo old, ISwapchain::DiffInfo now, bool bImageRecreated)
        {
            const bool bExtentChanged = (now.extent.width != old.extent.width ||
                                         now.extent.height != old.extent.height);
            const bool bPresentModeChanged = (old.presentMode != now.presentMode);

            if (bExtentChanged || bImageRecreated || bPresentModeChanged) {
                rebuildPresentationImages();
            }
        });

    _deleter.push("ScreenRT", [this](void*)
                  {
        if (_presentationPostProcessor) {
            _presentationPostProcessor->shutdown();
            _presentationPostProcessor.reset();
        }
        _presentationGraphExecutors.clear();
        _presentationImages.clear(); });
}

void RenderRuntime::rebuildPresentationImages()
{
    for (auto& executor : _presentationGraphExecutors) {
        if (executor) {
            executor->clear();
        }
    }
    _presentationGraphExecutors.clear();

    _presentationImages.clear();
    if (!_render) {
        return;
    }

    auto* swapchain = _render->getSwapchain() ? _render->getSwapchain()->as<VulkanSwapChain>() : nullptr;
    YA_CORE_ASSERT(swapchain != nullptr, "Presentation resources currently require VulkanSwapChain");

    _presentationGraphExecutors.reserve(swapchain->getImageCount());
    _presentationImages.reserve(swapchain->getImageCount());
    for (uint32_t imageIndex = 0; imageIndex < swapchain->getImageCount(); ++imageIndex) {
        _presentationGraphExecutors.push_back(std::make_unique<RenderGraphExecutor>(*_render->getResourceFactory()));
        _presentationImages.push_back(createPresentationRenderImage(*_render, *swapchain, imageIndex));
    }
}

void RenderRuntime::initCommandResources()
{
    std::vector<stdptr<ICommandBuffer>> cmdBufs;
    _render->allocateCommandBuffers(_render->getSwapchainImageCount(), cmdBufs);
    _commandBuffers.assign(cmdBufs.begin(), cmdBufs.end());
    _deleter.push("CmdBufs", [this](void*)
                  {
        _commandBuffers.clear(); });

    _offscreen.init(_render);
    _deleter.push("OffscreenTaskService", [this](void*)
                  { _offscreen.shutdown(); });
}

void RenderRuntime::initFrameServices()
{
    DeferredDeletionQueue::get().init(/*framesInFlight=*/1);

    // Owned derived-processing systems: gameplay resource binding and the
    // environment-lighting/terrain processors live with the render runtime
    // (injected narrow services only; never located through the Host).
    _gameplayResourceBinding = std::make_unique<GameplayResourceBinding>();
    _gameplayResourceBinding->setActiveSceneProvider(_activeSceneProvider);
    _gameplayResourceBinding->setFrameIndexProvider([this]() { return getFrameIndex(); });
    _gameplayResourceBinding->init();

    _environmentLightingProcessor = std::make_unique<EnvironmentLightingProcessor>();
    _environmentLightingProcessor->setRender(_render);
    _environmentLightingProcessor->setActiveSceneProvider(_activeSceneProvider);
    _environmentLightingProcessor->setFrameIndexProvider([this]() { return getFrameIndex(); });
    if (_hostServices) {
        _environmentLightingProcessor->setOffscreenJobQueueService(_hostServices->getOffscreenJobQueueService());
    }
    _environmentLightingProcessor->init();

    _terrainProcessor = std::make_unique<TerrainProcessor>();
    _terrainProcessor->setRender(_render);
    _terrainProcessor->setActiveSceneProvider(_activeSceneProvider);
    _terrainProcessor->setFrameIndexProvider([this]() { return getFrameIndex(); });
    _terrainProcessor->init();
}

void RenderRuntime::shutdown(bool bRenderAlreadyIdle)
{
    if (_render && !bRenderAlreadyIdle) {
        _render->waitIdle();
    }

    shutdownActivePipeline();
    // Owned derived-processing systems must release their GPU resources
    // before the render backend is destroyed.
    if (_environmentLightingProcessor) {
        _environmentLightingProcessor->shutdown();
        _environmentLightingProcessor.reset();
    }
    if (_terrainProcessor) {
        _terrainProcessor->shutdown();
        _terrainProcessor.reset();
    }
    if (_gameplayResourceBinding) {
        _gameplayResourceBinding->shutdown();
        _gameplayResourceBinding.reset();
    }
    getDebugRenderSystem().destroy();
    shutdownRuntimeServices();
    _deleter.clear();
    destroyRenderBackend();
}

void RenderRuntime::shutdownRuntimeServices()
{
    YA_CORE_ASSERT(_forwardPipeline == nullptr && _deferredPipeline == nullptr,
                   "shutdownRuntimeServices requires active pipelines to be torn down first");

    AssetManager::get()->setRender(nullptr);
    PrimitiveMeshCache::get().setRender(nullptr);
    ResourceRegistry::get().clearAll();
}

void RenderRuntime::destroyRenderBackend()
{
    if (!_render) {
        return;
    }

    DeferredDeletionQueue::get().flushAll();

    _render->destroy();
    delete _render;
    _render = nullptr;
}

void RenderRuntime::resetSkyboxPool()
{
    _sharedResourceProvider.resetSkyboxPool();
}

void RenderRuntime::resetEnvironmentLightingPool()
{
    _sharedResourceProvider.resetEnvironmentLightingPool();
}

} // namespace ya
