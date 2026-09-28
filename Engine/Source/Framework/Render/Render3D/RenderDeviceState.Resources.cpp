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

#include <format>
#include <string>

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
    initSurfacePresentations();
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
    // A seed for the first pipeline build only: the first frame's View rect
    // replaces it. Not a "current viewport" -- see the member's declaration.
    _initialViewExtent = Extent2D{
        .width  = desc.windowWidth,
        .height = desc.windowHeight,
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
                ShaderDesc::StageFile{.stage = EShaderStage::Vertex, .file = "Sprite2DScreen.slang"},
                ShaderDesc::StageFile{.stage = EShaderStage::Fragment, .file = "Sprite2DScreen.slang"},
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
    // A capture diagnoses a named window, so this names the one the device was
    // created for instead of letting the service pick "the" window. With
    // several windows presenting, which one a capture targets becomes an
    // explicit app choice (`AB4-2d`).
    IRenderSurfaceContext* captureSurface = (_render && _startupWindow) ? _render->findSurface(*_startupWindow) : nullptr;
    _diagnostics.init(_render, captureSurface, desc.bEnableRenderDoc, desc.renderDocDllPath, desc.renderDocCaptureOutputDir);
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
    // The device is created for this window: it is a startup surface (see
    // RenderCreateInfo::startupSurfaces), not a privileged one. The app names
    // its own surface through this window when it needs it.
    _startupWindow = nativeWindow;

    RenderCreateInfo renderCI{
        .renderAPI   = currentRenderAPI,
        .startupSurfaces = {
            StartupSurfaceDesc{
                .window      = nativeWindow,
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
            },
        },
        .disabledGraphicsCards = {},
    };

    _render = IRender::create(renderCI);
    YA_CORE_ASSERT(_render, "Failed to create IRender instance");
    // The backend consumes the shader service while building pipelines; the
    // runtime injects it right after backend creation so RHI/backend stay free
    // of host-layer lookups (initShaderSystems runs before this point).
    _render->setShaderStorage(_shaderStorage);
    _render->init(renderCI);

    // Label the window with the device that actually came up, now that the
    // backend can report it. A label is set once, where the fact exists, instead
    // of being re-formatted every tick by the app loop through a backend
    // downcast (see `IRender::getDeviceName`).
    if (!desc.windowTitle.empty()) {
        const std::string deviceName = _render->getDeviceName();
        nativeWindow->setTitle(deviceName.empty()
                                   ? desc.windowTitle
                                   : std::format("{}({})", desc.windowTitle, deviceName));
    }
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
        .initialViewWidth      = static_cast<int>(_initialViewExtent.width),
        .initialViewHeight     = static_cast<int>(_initialViewExtent.height),
    });

    _screenDrawPipelines.init(_render);
    _worldDrawPipelines.init(_render);
}

void RenderDeviceState::initSurfacePresentations()
{
    // Present targets are built on demand, one per surface the host actually
    // presents through (see acquireSurfacePresentation), so there is no GPU work
    // at init and no surface is privileged. What is registered here is only their
    // teardown, so every surface's imported images and format-specific write
    // pass are destroyed before the render backend, in the same ordered stack as
    // the rest of the device resources.
    _deleter.push("SurfacePresentations", [this](void*)
                  {
        for (auto& presentation : _surfacePresentations) {
            if (presentation) {
                presentation->shutdown();
            }
        }
        _surfacePresentations.clear(); });
}

SurfaceId RenderDeviceState::surfaceIdOf(IRenderSurfaceContext& surface) const
{
    // The device's registry is the answer to "which surface is this"; a device
    // that has none (an uninitialized state, or a caller's stand-in surface)
    // cannot name it, which is an answer too.
    return _render ? _render->findSurfaceId(surface) : SurfaceId{};
}

SurfacePresentation* RenderDeviceState::findSurfacePresentation(SurfaceId id) const
{
    if (!id.valid()) {
        return nullptr;
    }
    for (const auto& presentation : _surfacePresentations) {
        if (presentation && presentation->id() == id) {
            return presentation.get();
        }
    }
    return nullptr;
}

SurfacePresentation& RenderDeviceState::acquireSurfacePresentation(SurfaceId id, IRenderSurfaceContext& surface)
{
    if (SurfacePresentation* existing = findSurfacePresentation(id)) {
        return *existing;
    }

    auto presentation = std::make_unique<SurfacePresentation>();
    presentation->init(SurfacePresentation::InitDesc{
        .render  = _render,
        .present = &surface,
        .id      = id,
    });

    _surfacePresentations.push_back(std::move(presentation));
    return *_surfacePresentations.back();
}

void RenderDeviceState::reconcileSurfacePresentations()
{
    if (_surfacePresentations.empty()) {
        return;
    }

    // A present target is only meaningful while its surface is: its images are
    // imported views of that window's swapchain. Releasing it here keeps the
    // table from answering a later window that happens to own the same address.
    std::erase_if(_surfacePresentations,
                  [this](const std::unique_ptr<SurfacePresentation>& presentation)
                  {
                      if (!presentation) {
                          return true;
                      }
                      const SurfaceId id = presentation->id();
                      // An entry acquired without a registry id (a device with no
                      // backend) has nothing to reconcile against.
                      if (!id.valid()) {
                          return false;
                      }
                      if (_render && _render->findSurface(id) == presentation->surface()) {
                          return false;
                      }
                      presentation->shutdown();
                      return true;
                  });
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
    if (_render && _render->getResourceFactory()) {
        _viewTargets.init(*_render->getResourceFactory());
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
    _viewTargets.clear();
    _pipelineCoordinator.shutdown();
    _screenDrawPipelines.destroy();
    _worldDrawPipelines.destroy();
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

    _skinningCache.clear();
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
