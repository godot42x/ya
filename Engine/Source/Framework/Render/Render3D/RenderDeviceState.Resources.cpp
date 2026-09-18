#include "Render3D/RenderDeviceState.h"

#include "Render3D/Common/RenderRuntimeHostServices.h"
#include "Render3D/Services/DebugRenderSystem.h"
#include "RHI/Core/RenderTexture.h"
#include "RHI/Core/RenderResourceFactory.h"
#include "RHI/Core/Swapchain.h"
#include "Render3D/Pipelines/BasicPostprocessing.h"
#include "Resource/AssetManager.h"
#include "Core/Common/DeferredDeletionQueue.h"
#include "Core/Log.h"
#include "Render/Resources/FontManager.h"
#include "Resource/Mesh/PrimitiveMeshCache.h"
#include "Core/ResourceRegistry.h"
#include "RHI/Backend/TextureLibrary.h"
#include "RHI/RenderDefines.h"

namespace ya
{

DescriptorSetHandle RenderDeviceState::getSceneSkyboxDescriptorSet(Scene* scene)
{
    return _sharedResourceProvider.getSceneSkyboxDescriptorSet(scene);
}

DescriptorSetHandle RenderDeviceState::getSceneEnvironmentLightingDescriptorSet(Scene* scene)
{
    return _sharedResourceProvider.getSceneEnvironmentLightingDescriptorSet(scene);
}

EnvironmentLightingSceneResources RenderDeviceState::resolveSceneEnvironmentLightingResources(Scene* scene) const
{
    return _sharedResourceProvider.resolveSceneEnvironmentLightingResources(scene);
}

void RenderDeviceState::init(const InitDesc& desc)
{
    YA_PROFILE_FUNCTION_LOG();
    YA_CORE_ASSERT(desc.hostServices != nullptr, "RenderDeviceState init requires host services");

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

void RenderDeviceState::initRuntimeState(const InitDesc& desc)
{
    _hostServices       = desc.hostServices;
    _offscreenScheduler = desc.offscreenScheduler;
    _clockState                 = desc.clockState;
    _environmentLightingProvider = desc.environmentLightingProvider;

    currentRenderAPI = ERenderAPI::Vulkan;
    _pipelineViewportRect = Rect2D{
        .pos    = {0.0f, 0.0f},
        .extent = {static_cast<float>(desc.windowWidth), static_cast<float>(desc.windowHeight)},
    };
}

void RenderDeviceState::initShaderSystems()
{
    _shaderStorage = std::make_shared<ShaderStorage>(
        ShaderProcessorFactory()
            .withShaderStoragePath("Engine/Shader/Slang")
            .withCachedStoragePath("Engine/Intermediate/Shader/Slang")
            .FactoryNew());

    _shaderStorage->preloadAsync({
        ShaderDesc{.shaderName = "Unlit.slang"},
        ShaderDesc{.shaderName = "SimpleMaterial.slang"},
        ShaderDesc{
            .sourceMode = ShaderDesc::ESourceMode::StageFiles,
            .stageFiles = {
                ShaderDesc::StageFile{.stage = EShaderStage::Vertex, .file = "Sprite2D.slang"},
                ShaderDesc::StageFile{.stage = EShaderStage::Fragment, .file = "Sprite2D.slang"},
            },
        },
        ShaderDesc{.shaderName = "DebugRender.slang"},
        ShaderDesc{.shaderName = "DebugPrimitiveLine.slang"},
        ShaderDesc{.shaderName = "DebugPrimitiveShape.slang"},
        ShaderDesc{.shaderName = "Skybox.slang"},
        ShaderDesc{.shaderName = "CombineShadowMappingGenerate.slang"},
        ShaderDesc{.shaderName = "Shadow/PointShadowCull.comp.slang"},
        ShaderDesc{.shaderName = "Shadow/PointShadowIndirect.slang"},
        ShaderDesc{.shaderName = "Misc/pbr_generate_brdf_lut.slang"},
    });
    _deleter.push("ShaderStorage", [this](void*)
                  { _shaderStorage.reset(); });
}

void RenderDeviceState::initDiagnostics(const InitDesc& desc)
{
    _diagnostics.init(_render, desc.bEnableRenderDoc, desc.renderDocDllPath, desc.renderDocCaptureOutputDir);
    _deleter.push("RenderDiagnostics", [this](void*)
                  { _diagnostics.shutdown(); });
}

void RenderDeviceState::initRenderBackend(const InitDesc& desc)
{
    YA_CORE_ASSERT(_hostServices != nullptr, "RenderDeviceState requires host services before initializing render backend");
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

void RenderDeviceState::initResourceCaches()
{
    TextureLibrary::get().init(_render);
    AssetManager::get()->setRender(_render);
    PrimitiveMeshCache::get().setRender(_render);

    ResourceRegistry::get().registerCache(&PrimitiveMeshCache::get(), 100);
    ResourceRegistry::get().registerCache(&TextureLibrary::get(), 90);
    ResourceRegistry::get().registerCache(FontManager::get(), 80);
    ResourceRegistry::get().registerCache(AssetManager::get(), 70);
}

void RenderDeviceState::initSharedRenderResources()
{
    _sharedResourceProvider.init(_render, _environmentLightingProvider);

    _deleter.push("RenderBindings", [this](void*)
                  { _sharedResourceProvider.shutdown(); });

    _shaderStorage->waitForPreload();
    // Compile check for the Forward lit path: PhongLit.slang is warmed here so a
    // broken shader surfaces during init instead of at first pipeline build.
    _shaderStorage->validate(ShaderDesc{.shaderName = "PhongLit.slang"});

    _pipelineCoordinator.init(PipelineCoordinator::InitDesc{
        .render                = _render,
        .hostServices          = _hostServices,
        .sharedResourceProvider = &_sharedResourceProvider,
        .debugRenderSystem     = &DebugRenderSystem::get(),
        .viewportWidth         = static_cast<int>(_pipelineViewportRect.extent.x),
        .viewportHeight        = static_cast<int>(_pipelineViewportRect.extent.y),
        .reapplyViewportSink   = [this]()
        {
            if (_pipelineViewportRect.extent.x > 0.0f && _pipelineViewportRect.extent.y > 0.0f) {
                if (auto* pipeline = getActivePipeline()) {
                    pipeline->onViewportResized(_pipelineViewportRect);
                }
            }
        },
    });
}

void RenderDeviceState::initPresentationResources()
{
    // Main world window display compose only. Acquire/present stay on the host
    // FPresentFrame coordinator; this service never calls begin/end.
    _presentationGraphService.init(PresentationGraphService::InitDesc{
        .render  = _render,
        .present = _render->getPrimarySurfaceContext(),
        .viewportDisplayImageProvider = [this]()
        {
            return getViewportDisplayImageShared();
        },
    });

    _deleter.push("ScreenRT", [this](void*)
                  {
        _presentationGraphService.shutdown(); });
}

void RenderDeviceState::initCommandResources()
{
    std::vector<stdptr<ICommandBuffer>> cmdBufs;
    _render->allocateCommandBuffers(MAX_FLIGHTS_IN_FLIGHT, cmdBufs);
    _commandBuffers.assign(cmdBufs.begin(), cmdBufs.end());
    _deleter.push("CmdBufs", [this](void*)
                  {
        _commandBuffers.clear(); });

    if (!_submissions.init(_render)) {
        YA_CORE_ERROR("RenderDeviceState failed to initialize the submission pool");
    }
    _deleter.push("RenderSubmissionPool", [this](void*)
                  { _submissions.destroy(); });

    _offscreen.init(_render);
    _deleter.push("OffscreenTaskService", [this](void*)
                  { _offscreen.shutdown(); });
}

void RenderDeviceState::initFrameServices()
{
    DeferredDeletionQueue::get().init(/*framesInFlight=*/1);

    // Owned derived-processing systems: gameplay resource binding and the
    // environment-lighting/terrain processors live with the render runtime
    // (injected narrow services only; never located through the Host).
    _gameplayResourceBinding = std::make_unique<GameplayResourceBinding>();
    _gameplayResourceBinding->setHostTickProvider([this]() { return getHostTick(); });
    _gameplayResourceBinding->init();

    _environmentLightingProcessor = std::make_unique<EnvironmentLightingProcessor>();
    _environmentLightingProcessor->setRender(_render);
    _environmentLightingProcessor->setHostTickProvider([this]() { return getHostTick(); });
    if (_hostServices) {
        _environmentLightingProcessor->setOffscreenJobQueueService(_hostServices->getOffscreenJobQueueService());
    }
    _environmentLightingProcessor->init();

    _terrainProcessor = std::make_unique<TerrainProcessor>();
    _terrainProcessor->setRender(_render);
    _terrainProcessor->setHostTickProvider([this]() { return getHostTick(); });
    _terrainProcessor->init();
}

void RenderDeviceState::shutdown(bool bRenderAlreadyIdle)
{
    if (_render && !bRenderAlreadyIdle) {
        _render->waitIdle();
    }

    _submissions.clear();
    _viewOutputs.clear();
    _publishedOutputFlight = MAX_FLIGHTS_IN_FLIGHT;
    _publishedOutputViewId = 0;
    _pipelineCoordinator.shutdown();
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

void RenderDeviceState::shutdownRuntimeServices()
{
    YA_CORE_ASSERT(!_pipelineCoordinator.hasAnyPipeline(),
                   "shutdownRuntimeServices requires active pipelines to be torn down first");

    AssetManager::get()->setRender(nullptr);
    PrimitiveMeshCache::get().setRender(nullptr);
    ResourceRegistry::get().clearAll();
}

void RenderDeviceState::destroyRenderBackend()
{
    if (!_render) {
        return;
    }

    DeferredDeletionQueue::get().flushAll();

    _render->destroy();
    delete _render;
    _render = nullptr;
}

void RenderDeviceState::resetSkyboxPool()
{
    _sharedResourceProvider.resetSkyboxPool();
}

void RenderDeviceState::resetEnvironmentLightingPool()
{
    _sharedResourceProvider.resetEnvironmentLightingPool();
}

} // namespace ya
