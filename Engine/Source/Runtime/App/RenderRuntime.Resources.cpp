#include "RenderRuntime.h"

#include "App.h"
#include "DebugRenderSystem.h"
#include "Render/Core/IRenderTarget.h"
#include "Render/Core/Swapchain.h"
#include "Resource/AssetManager.h"
#include "Resource/DeferredDeletionQueue.h"
#include "Resource/Font/FontManager.h"
#include "Resource/Mesh/PrimitiveMeshCache.h"
#include "Resource/ResourceRegistry.h"
#include "Resource/Texture/TextureLibrary.h"

namespace ya
{

DescriptorSetHandle RenderRuntime::getSceneSkyboxDescriptorSet(Scene* scene)
{
    return _sharedResourceProvider.getSceneSkyboxDescriptorSet(scene);
}

DescriptorSetHandle RenderRuntime::getSceneEnvironmentLightingDescriptorSet(Scene* scene)
{
    return _sharedResourceProvider.getSceneEnvironmentLightingDescriptorSet(scene);
}

RenderSharedResourceProvider::EnvironmentLightingTextureSet RenderRuntime::resolveSceneEnvironmentLightingTextures(Scene* scene) const
{
    return _sharedResourceProvider.resolveSceneEnvironmentLightingTextures(scene);
}

void RenderRuntime::init(const InitDesc& desc)
{
    YA_PROFILE_FUNCTION_LOG();
    YA_CORE_ASSERT(desc.app && desc.appDesc, "RenderRuntime init requires App and AppDesc");

    const AppDesc& appDesc = *desc.appDesc;

    initRuntimeState(desc);
    initShaderSystems();
    initDiagnostics(appDesc);
    initRenderBackend(appDesc);
    initResourceCaches();
    initSharedRenderResources();
    initPresentationResources();
    initCommandResources();
    initFrameServices();
}

void RenderRuntime::initRuntimeState(const InitDesc& desc)
{
    _app = desc.app;

    const AppDesc& appDesc = *desc.appDesc;
    currentRenderAPI       = ERenderAPI::Vulkan;
    _viewportRect          = Rect2D{
                 .pos    = {0.0f, 0.0f},
                 .extent = {static_cast<float>(appDesc.width), static_cast<float>(appDesc.height)},
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
        ShaderDesc{.shaderName = "Sprite2D_Screen.glsl"},
        ShaderDesc{.shaderName = "Sprite2D_World.glsl"},
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

void RenderRuntime::initDiagnostics(const AppDesc& appDesc)
{
    _diagnostics.init(_render, appDesc);
    _deleter.push("RenderDiagnostics", [this](void*)
                  { _diagnostics.shutdown(); });
}

void RenderRuntime::initRenderBackend(const AppDesc& appDesc)
{
    const bool bNeedPresentationTransferSrc = appDesc.automation.screenshotPath.has_value() &&
                                              appDesc.automation.screenshotTarget == EAutomationScreenshotTarget::Editor;

    RenderCreateInfo renderCI{
        .renderAPI   = currentRenderAPI,
        .swapchainCI = SwapchainCreateInfo{
            .imageFormat        = App::LINEAR_FORMAT,
            .bVsync             = false,
            .minImageCount      = 3,
            .bEnableTransferSrc = bNeedPresentationTransferSrc,
            .width              = static_cast<uint32_t>(appDesc.width),
            .height             = static_cast<uint32_t>(appDesc.height),
        },
    };

    _render = IRender::create(renderCI);
    YA_CORE_ASSERT(_render, "Failed to create IRender instance");
    _render->init(renderCI);

}

void RenderRuntime::initResourceCaches()
{
    TextureLibrary::get().init(_render);

    ResourceRegistry::get().registerCache(&PrimitiveMeshCache::get(), 100);
    ResourceRegistry::get().registerCache(&TextureLibrary::get(), 90);
    ResourceRegistry::get().registerCache(FontManager::get(), 80);
    ResourceRegistry::get().registerCache(AssetManager::get(), 70);
}

void RenderRuntime::initSharedRenderResources()
{
    _sharedResourceProvider.init(_render, _app);

    _deleter.push("RenderBindings", [this](void*)
                  { _sharedResourceProvider.shutdown(); });

    _shaderStorage->waitForPreload();
    _shaderStorage->validate(ShaderDesc{.shaderName = "PhongLit/PhongLit.glsl"});

    initActivePipeline();
}

void RenderRuntime::initPresentationResources()
{
    _presentationGraphExecutor = _render ? std::make_unique<RenderGraphExecutor>(*_render->getResourceFactory()) : nullptr;
    _screenRenderPass = nullptr;
    _screenRT         = ya::createRenderTarget(RenderTargetCreateInfo{
        .label            = "Final RenderTarget",
        .renderingMode    = ERenderingMode::DynamicRendering,
        .bSwapChainTarget = true,
        .attachments      = {
            .colorAttach = {
                AttachmentDescription{
                    .index          = 0,
                    .format         = _render->getSwapchain()->getFormat(),
                    .samples        = ESampleCount::Sample_1,
                    .loadOp         = EAttachmentLoadOp::Clear,
                    .storeOp        = EAttachmentStoreOp::Store,
                    .stencilLoadOp  = EAttachmentLoadOp::DontCare,
                    .stencilStoreOp = EAttachmentStoreOp::DontCare,
                    .initialLayout  = EImageLayout::PresentSrcKHR,
                    .finalLayout    = EImageLayout::PresentSrcKHR,
                    .usage          = EImageUsage::ColorAttachment,
                },
            },
        },
    });

    _render->getSwapchain()->onRecreate.addLambda(
        this,
        [this](ISwapchain::DiffInfo old, ISwapchain::DiffInfo now, bool bImageRecreated)
        {
            const bool bExtentChanged = (now.extent.width != old.extent.width ||
                                         now.extent.height != old.extent.height);
            const bool bPresentModeChanged = (old.presentMode != now.presentMode);

            if (bExtentChanged) {
                _screenRT->setExtent(Extent2D{
                    .width  = now.extent.width,
                    .height = now.extent.height,
                });
            }

            if (bImageRecreated || bPresentModeChanged) {
                _screenRT->recreate();
            }
        });

    _deleter.push("ScreenRT", [this](void*)
                  {
        _presentationGraphExecutor.reset();
        if (_screenRT) {
            _screenRT->destroy();
            _screenRT.reset();
        }
        _screenRenderPass.reset(); });
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
    ImGuiManager::get().init(_render, nullptr);
    _render->waitIdle();

    DeferredDeletionQueue::get().init(/*framesInFlight=*/1);
}

void RenderRuntime::shutdown()
{
    shutdownRuntimeServices();
    shutdownActivePipeline();
    getDebugRenderSystem().destroy();
    _deleter.clear();
    destroyRenderBackend();
}

void RenderRuntime::shutdownRuntimeServices()
{
    if (_render) {
        _render->waitIdle();
    }

    ImGuiManager::get().shutdown();
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
