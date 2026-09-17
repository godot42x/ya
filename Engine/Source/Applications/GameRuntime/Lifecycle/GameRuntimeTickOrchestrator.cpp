#include "GameRuntime/Lifecycle/GameRuntimeTickOrchestrator.h"

#include "GameRuntime/App.h"
#include "GameRuntime/HostViewState.h"
#include "GameRuntime/AppRenderState.h"
#include "GameRuntime/Automation/AppAutomationControlService.h"
#include "GameRuntime/Lifecycle/AppAutomation.h"
#include "HostSdlEventSource.h"
#include "GameRuntime/Utility/FPSCtrl.h"
#include "GameRuntime/Utility/SceneCameraQuery.h"
#include "Render3D/Services/RenderDiagnosticsService.h"

#include "Core/Async/TaskQueue.h"
#include "Core/Manager/Facade.h"
#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Core/System/FileWatcher.h"

#include "ECS/Component/2D/BillboardComponent.h"
#include "ECS/Systems/Components/CameraComponent.h"
#include "Scene3D/TransformComponent.h"
#include "ECS/Systems/LuaScriptingSystem.h"

#include "RHI/Backend/Vulkan//VulkanRender.h"
#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/PresentFrame.h"
#include "RHI/Core/RenderSurfaceContext.h"
#include "RHI/NativeWindow.h"
#include "RHI/Render.h"
#include "RHI/RenderDefines.h"

#include "Render2D/Render2D.h"
#include "Render3D/Common/RenderFrameInputs.h"
#include "Render3D/RenderDeviceState.h"
#include "Render3D/RenderFrameCoordinator.h"
#include "Render3D/Material/Material.h"
#include "GameRuntime/Lifecycle/HostSceneExtract.h"
#include "GameRuntime/Utility/RenderFrameExtractor.h"
#include "Scene/Core/Scene.h"
#include "Scene/Runtime/SceneManager.h"

#include "Core/Math/Math.h"
#include "ECS/Component.h"
#include "Render3D/Common/CameraFrustumOverlay.h"
#include "Render3D/Common/ViewCompose.h"

#include <algorithm>
#include <format>
#include <functional>
#include <vector>

namespace ya
{

namespace
{
void syncRuntimeCameraAspect(Entity* runtimeCamera, const Extent2D& viewportExtent)
{
    if (!runtimeCamera || !runtimeCamera->isValid() || !runtimeCamera->hasComponent<CameraComponent>()) {
        return;
    }
    if (viewportExtent.width == 0 || viewportExtent.height == 0) {
        return;
    }

    auto* camera = runtimeCamera->getComponent<CameraComponent>();
    if (!camera->_fixedAspectRatio) {
        camera->setAspectRatio(static_cast<float>(viewportExtent.width) /
                               static_cast<float>(viewportExtent.height));
    }
}

} // namespace

int GameRuntimeTickOrchestrator::iterate(App& app, float dt)
{
    YA_PROFILE_FUNCTION()
    YA_PERF_FUNCTION(perf::metric::cpuTimeMs(), perf::domain::render());

    YA_PERF_TICK_SCOPE(
        perf::sample::hostTick(),
        perf::metric::cpuTimeMs(),
        perf::domain::render(),
        perf::sample::tickUnaccounted(),
        perf::sample::tickEventPump(),
        perf::sample::tickFpsControl(),
        perf::sample::tickLogic(),
        perf::sample::tickRender(),
        perf::sample::tickMainThreadCallbacks(),
        perf::sample::tickAutomation());

    {
        YA_PROFILE_SCOPE("Tick/FpsControl");
        YA_PERF_SCOPE(perf::sample::tickFpsControl(), perf::metric::cpuTimeMs(), perf::domain::game());
        dt += FPSControl::get()->update(dt);
    }

    if (!app._bPause) {
        YA_PROFILE_SCOPE("Tick/Logic");
        YA_PERF_SCOPE(perf::sample::tickLogic(), perf::metric::cpuTimeMs(), perf::domain::game());
        tickLogic(app, dt);
    }
    {
        YA_PROFILE_SCOPE("Tick/Render");
        YA_PERF_SCOPE(perf::sample::tickRender(), perf::metric::cpuTimeMs(), perf::domain::render());
        tickRender(app, dt);
    }
    {
        YA_PROFILE_SCOPE("Tick/MainThreadCallbacks");
        YA_PERF_SCOPE(perf::sample::tickMainThreadCallbacks(), perf::metric::cpuTimeMs(), perf::domain::game());
        YA_PERF_SCOPE(perf::sample::tickRenderCallbacks(), perf::metric::cpuTimeMs(), perf::domain::render());
        TaskQueue::get().processMainThreadCallbacks();
    }
    ++App::_hostTick;

    auto& renderServices = app.getRenderServices();
    auto* device         = renderServices.getDeviceState();
    if (auto* automationControl = app.getAutomationControlService()) {
        automationControl->onTickCompleted(app,
                                            renderServices.getRender(),
                                            device ? device->getPostprocessOutputImageShared() : nullptr,
                                            device ? device->getActiveViewportImageShared() : nullptr,
                                            device ? device->getPresentationImageShared() : nullptr,
                                            App::_hostTick);
    }

    if (AppAutomation::isTickAutomationEnabled(app)) {
        YA_PROFILE_SCOPE("Tick/Automation");
        YA_PERF_SCOPE(perf::sample::tickAutomation(), perf::metric::cpuTimeMs(), perf::domain::render());
        auto* diagnosticsService = device ? &device->getDiagnosticsService() : nullptr;

        AppAutomation::onTickCompleted(app,
                                        AppAutomationTickContext{
                                            .render                     = renderServices.getRender(),
                                            .postprocessImage           = device ? device->getPostprocessOutputImageShared() : nullptr,
                                            .viewportImage              = device ? device->getActiveViewportImageShared() : nullptr,
                                            .presentationImage          = device ? device->getPresentationImageShared() : nullptr,
                                            .requestRenderDocCapture    = diagnosticsService
                                                                            ? [diagnosticsService]()
                                                                           { return diagnosticsService->requestAutomationRenderDocCapture(); }
                                                                            : std::function<bool()>{},
                                            .isRenderDocCapturePending  = diagnosticsService
                                                                            ? [diagnosticsService]()
                                                                             { return diagnosticsService->isAutomationRenderDocCapturePending(); }
                                                                            : std::function<bool()>{},
                                            .isRenderDocCaptureTerminal = diagnosticsService
                                                                            ? [diagnosticsService]()
                                                                              { return diagnosticsService->isAutomationRenderDocCaptureTerminal(); }
                                                                            : std::function<bool()>{},
                                            .getRenderDocCapturePath    = diagnosticsService
                                            ? [diagnosticsService]() -> const std::string&
                                            { return diagnosticsService->getAutomationRenderDocCapturePath(); }
                                            : std::function<const std::string&()>{},
                                            .getRenderDocPassSummaryPath = diagnosticsService
                                            ? [diagnosticsService]() -> const std::string&
                                            { return diagnosticsService->getAutomationRenderDocPassSummaryPath(); }
                                            : std::function<const std::string&()>{},
                                            .hostTick = App::_hostTick,
                                        });
    }

    return 0;
}

void GameRuntimeTickOrchestrator::tickLogic(App& app, float dt)
{
    YA_PROFILE_FUNCTION()
    {
        YA_PROFILE_SCOPE("Logic/TaskManager");
        app.taskManager.update();
    }
    {
        YA_PROFILE_SCOPE("Logic/TimerManager");
        facade().timerManager.onUpdate(dt);
    }
    {
        YA_PROFILE_SCOPE("Logic/AppAutomationControlService");
        if (auto* automationControl = app.getAutomationControlService()) {
            automationControl->update(app);
        }
    }
    {
        YA_PROFILE_SCOPE("Logic/ViewportSync");
        syncViewportState(app);
    }
    {
        YA_PROFILE_SCOPE("Logic/Systems");
        for (auto& sys : app._systems) {
            sys->onUpdate(dt);
        }
    }

    if (app.getSceneServices().getActiveScene()) {
        YA_PROFILE_SCOPE("Logic/RuntimeCamera");
        auto* device = app.getRenderServices().getDeviceState();
        const Extent2D viewportExtent = resolveViewportExtent(app,
                                                              device,
                                                              app._renderState->hostView.viewportRect);
        syncRuntimeCameraAspect(findPrimaryCamera(*app.getSceneServices().getActiveScene()), viewportExtent);
    }

    {
        YA_PROFILE_SCOPE("Logic/Render2DUpdate");
        Render2D::onUpdate(dt);
    }

    switch (app._appState) {
    case AppState::Stopped:
        break;
    case AppState::Simulation:
    case AppState::Runtime:
    {
        YA_PROFILE_SCOPE("Logic/Lua");
        app._luaScriptingSystem->onUpdate(dt);
    } break;
    }

    if (auto* watcher = FileWatcher::get()) {
        YA_PROFILE_SCOPE("Logic/FileWatcher");
        YA_PERF_SCOPE(perf::sample::appFileWatcher(), perf::metric::cpuTimeMs(), perf::domain::game());
        watcher->poll();
    }

    app.tickModules(dt);
    {
        YA_PROFILE_SCOPE("Logic/InputPostUpdate");
        app.inputManager.postUpdate();
    }

    {
        YA_PROFILE_SCOPE("Logic/InputPreUpdate");
        app.inputManager.preUpdate();
    }
    auto* render = app.getRenderServices().getRender();
    if (!render) {
        return;
    }
    auto        vkRender       = render->as<VulkanRender>();
    auto        nativeWindow   = render->primaryWindow();
    std::string title          = std::format("{}({})", app._ci.title, vkRender->_selectedDeviceInfo.deviceName);
    if (nativeWindow) {
        nativeWindow->setTitle(title);
    }
}

void GameRuntimeTickOrchestrator::syncViewportState(App& app)
{
    (void)app;
}

Extent2D GameRuntimeTickOrchestrator::resolveViewportExtent(const App& app, RenderDeviceState* device, const Rect2D& viewportRect)
{
    if (device) {
        Extent2D extent = device->getViewportExtent();
        if (extent.width > 0 && extent.height > 0) {
            return extent;
        }
    }

    if (viewportRect.extent.x > 0 && viewportRect.extent.y > 0) {
        return Extent2D::fromVec2(viewportRect.extent);
    }

    return Extent2D{
        .width  = static_cast<uint32_t>(app._windowSize.x),
        .height = static_cast<uint32_t>(app._windowSize.y),
    };
}

void GameRuntimeTickOrchestrator::prepareHostViewState(App& app, float dt)
{
    (void)dt;

    // Host geometry only: the surface area a view renders into, its framebuffer
    // scale and the clock. The world camera is not host state; whichever
    // producer declares the primary view supplies it (see tickRender).
    HostViewState& hostView = app._renderState->hostView;
    if (!app.getRenderServices().getDeviceState()) {
        hostView = {};
        return;
    }

    hostView.clock.hostTick      = App::_hostTick;
    hostView.clock.elapsedTimeMS = app.getElapsedTimeMS();
    hostView.view       = glm::mat4(1.0f);
    hostView.projection = glm::mat4(1.0f);
    hostView.cameraPos  = glm::vec3(0.0f);
    if (hostView.viewportRect.extent.x <= 0.0f || hostView.viewportRect.extent.y <= 0.0f) {
        hostView.viewportRect = Rect2D{
            .pos    = {0.0f, 0.0f},
            .extent = app._windowSize,
        };
    }
}

uint32_t GameRuntimeTickOrchestrator::resolveFlightIndex(const App& app)
{
    auto* render = app.getRenderServices().getRender();
    if (!render) {
        return 0;
    }

    auto* present = render->getPrimarySurfaceContext();
    if (!present) {
        return 0;
    }

    // Surface present flight before this frame's begin(). Single-window
    // coincidence: end() advances after present, so this is the slot begin()
    // will wait. World recording uses this flight, not swapchain imageIndex.
    return present->getCurrentFrameIndex() % MAX_FLIGHTS_IN_FLIGHT;
}

std::vector<RenderOverlaySprite2D> GameRuntimeTickOrchestrator::buildScreenOverlaySprites(const App& app)
{
    std::vector<RenderOverlaySprite2D> sprites;
    if (app._appMode != AppMode::Drawing || app.clicked.empty()) {
        return sprites;
    }

    sprites.reserve(app.clicked.size());
    for (size_t idx = 0; idx < app.clicked.size(); ++idx) {
        const auto& screenPos     = app.clicked[idx];
        auto        textureHandle = idx % 2 == 0
                                      ? AssetManager::get()->getTextureByName("uv1")
                                      : AssetManager::get()->getTextureByName("face");
        auto*       texture       = textureHandle.get();
        YA_CORE_ASSERT(texture, "Texture not found");

        RenderOverlaySprite2D sprite;
        sprite.viewportPos = screenPos;
        sprite.size        = {50.0f, 50.0f};
        sprite.texture     = texture;
        sprites.push_back(sprite);
    }

    return sprites;
}

void GameRuntimeTickOrchestrator::tickRender(App& app, float dt)
{
    auto* device      = app.getRenderServices().getDeviceState();
    auto* coordinator = app.getRenderServices().getFrameCoordinator();
    if (!device || !coordinator) {
        return;
    }

    app.prepareModulesForRender(dt);
    {
        YA_PROFILE_SCOPE("Render/PrepareRenderFrameState");
        prepareHostViewState(app, dt);
    }

    auto& diagnostics = device->getDiagnosticsService();
    diagnostics.onFrameBegin();

    struct DiagnosticsGuard
    {
        RenderDiagnosticsService* diagnostics = nullptr;

        ~DiagnosticsGuard()
        {
            if (diagnostics) {
                diagnostics->onFrameEnd();
            }
        }
    } diagnosticsGuard{.diagnostics = &diagnostics};

    device->getOffscreenTaskService().tick(app.getTaskManager());

    const uint32_t flightIndex = resolveFlightIndex(app);

    auto* scene = app._sceneManager ? app._sceneManager->getActiveScene() : nullptr;
    HostViewState& hostView = app._renderState->hostView;

    auto& sceneScheduler = app._renderState->sceneRenderScheduler;
    sceneScheduler.beginTick(App::_hostTick);
    struct SceneSchedulerGuard
    {
        SceneRenderScheduler* scheduler = nullptr;
        ~SceneSchedulerGuard()
        {
            if (scheduler) {
                scheduler->clearTick();
            }
        }
    } sceneSchedulerGuard{.scheduler = &sceneScheduler};

    // Declare this tick's views. Every owner declares its own (the game
    // viewport, the editor's authoring viewport, the camera preview below), so a
    // view exists exactly because somebody asked for it: no layer has to flip a
    // global switch to hide a view another layer declared.
    const SceneViewCollectContext collectContext{
        .activeScene    = scene,
        .viewportRect   = hostView.viewportRect,
        .viewportExtent = resolveViewportExtent(app, device, hostView.viewportRect),
        .hostTick       = App::_hostTick,
        .deltaTime      = dt,
    };
    SceneViewCollector collector;
    for (ISceneViewProducer* producer : app._renderState->viewProducers) {
        if (producer) {
            producer->collectSceneViews(collectContext, collector);
        }
    }
    for (const SceneViewDesc& view : collector.views()) {
        (void)sceneScheduler.submit(view);
    }

    // The primary view's camera is what the host reports as "the world view":
    // the camera packet and the offscreen extent follow the declaration instead
    // of an injected copy of it.
    const SceneViewDesc* primaryView = nullptr;
    for (const SceneViewDesc& view : collector.views()) {
        if (view.viewId == kPrimarySceneViewId) {
            primaryView = &view;
            break;
        }
    }
    if (primaryView) {
        hostView.view       = primaryView->view;
        hostView.projection = primaryView->projection;
        hostView.cameraPos  = primaryView->cameraPos;
    }
    const glm::mat4 viewProjection = makeCameraViewProjection(hostView.projection, hostView.view);

    // Extraction is its own step: seal() only grouped the declarations, so
    // Scene/ECS content is read here and nowhere earlier.
    ExtractedSceneRender sceneRender =
        extractHostSceneSnapshots(sceneScheduler.seal(), device->getTerrainProcessor());

    // Animation policy input: poses are consumed by the world pipeline, so the
    // honest question is "did the renderer produce content for this Scene", not
    // "is some viewport's world switch on". One tick of lag by construction --
    // systems run before views are declared.
    app._renderState->renderedScenesLastTick = renderedScenes(sceneRender.plan());

    auto& viewFrames = app._renderState->viewFrameDataPerFlight[flightIndex];
    sceneRender.pairViewFrames(viewFrames);
    if (!sceneRender.empty()) {
        YA_PERF_SCOPE(perf::sample::renderExtract(), perf::metric::cpuTimeMs(), perf::domain::render());
        YA_PROFILE_SCOPE("RenderFrameExtractor::sceneSnapshot");
        for (const SceneViewRecording& recording : sceneRender.views()) {
            const SceneViewportTask& task      = *recording.task;
            const SceneViewDesc&     desc      = task.desc;
            RenderFrameData&         frameData = *recording.frameData;
            RenderFrameExtractor::prepareView(
                RenderFrameExtractor::ViewPrepareInput{
                    .view = desc.view,
                    .projection = desc.projection,
                    .viewProjection = desc.viewProjection(),
                    .cameraPos = desc.cameraPos,
                    .viewportExtent = Extent2D::fromVec2(desc.viewportRect.extent),
                    .viewOwner = desc.viewOwner,
                    .viewFeatures = desc.features,
                    .frameIndex = App::_hostTick,
                    .deltaTime = dt,
                    .shadowSettings = &app.getRenderServices().getShadowSettings(),
                },
                sceneRender.snapshotFor(task),
                frameData);
        }
    }

    CameraFrameInput cameraFrame{
        .flightIndex              = flightIndex,
        .frameIndex               = App::_hostTick,
        .deltaTime                = dt,
        .viewFeatures             = primaryView ? primaryView->features : toMask(ERenderFeature::Game),
        .view                     = hostView.view,
        .projection               = hostView.projection,
        .viewProjection           = viewProjection,
        .cameraPos                = hostView.cameraPos,
        .viewportRect             = hostView.viewportRect,
        .viewportFrameBufferScale = hostView.viewportFrameBufferScale,
        .frameData                = &viewFrames.front(),
        .shadowSettings           = &app.getRenderServices().getShadowSettings(),
    };

    auto screenOverlaySprites = GameRuntimeTickOrchestrator::buildScreenOverlaySprites(app);

    // Game UI: build the immutable frame snapshot BEFORE the RenderGraph.
    // Command recording consumes only this packet; the live WidgetTree is
    // never touched while recording. Runtime/simulation only (standalone game
    // and PIE); the editor's 3D authoring viewport has no game UI.
    UIFrameSnapshot          uiFrameSnapshot;
    const UIFrameSnapshot*   pUiFrameSnapshot = nullptr;
    if ((app.isRuntimeMode() || app.isSimulationMode()) && scene) {
        if (auto* gameUIHost = app.getGameUIHost()) {
            gameUIHost->setPresentation(cameraFrame.viewportRect,
                                        glm::vec2(cameraFrame.viewportFrameBufferScale));
            uiFrameSnapshot  = gameUIHost->buildSnapshot();
            pUiFrameSnapshot = &uiFrameSnapshot;
        }
    }

    cameraFrame.overlay         = {
        .screenSprites = &screenOverlaySprites,
    };
    cameraFrame.uiFrameSnapshot = pUiFrameSnapshot;

    IRender*       render        = device->getRender();
    FPresentFrame  presentFrame{.surface = render ? render->getPrimarySurfaceContext() : nullptr};
    {
        YA_PERF_SCOPE(perf::sample::renderBegin(), perf::metric::cpuTimeMs(), perf::domain::render());
        if (!acquirePresentFrame(presentFrame)) {
            app.presentModuleExtras(dt);
            return;
        }
    }
    if (!presentFrame.acquired()) {
        submitPresentFrame(presentFrame, {});
        app.presentModuleExtras(dt);
        return;
    }

    ICommandBuffer* recorded = coordinator->record(RenderFramePlan{
        .sceneRender = std::move(sceneRender),
        .camera = cameraFrame,
        .viewCompose = {
            .recordCompose = [&app, dt](ICommandBuffer* commandBuffer)
            {
                if (commandBuffer) {
                    app.recordModuleViewportCompose(*commandBuffer, dt);
                } },
        },
        .displayCompose = {
            .extensions = {
                .recordBeforeExtensions = [&app, dt](ICommandBuffer* commandBuffer)
                {
                    if (commandBuffer) {
                        app.recordModuleBeforePresentation(*commandBuffer, dt);
                    } },
                .recordExtensions = [&app, dt](ICommandBuffer* commandBuffer)
                {
                    if (commandBuffer) {
                        app.recordModulePresentation(*commandBuffer, dt);
                    } },
                .appendCapture = [&app](RenderGraph& graph, RGTextureHandle presentationOutput, Extent2D presentationExtent)
                {
                    bool bAppended = AppAutomation::appendPresentationCapture(app.getHostTick(),
                                                                              graph,
                                                                              presentationOutput,
                                                                              presentationExtent);
                    if (auto* automationControl = app.getAutomationControlService()) {
                        bAppended = automationControl->appendPresentationCapture(app.getHostTick(),
                                                                                 graph,
                                                                                 presentationOutput,
                                                                                 presentationExtent) ||
                                    bAppended;
                    }
                    return bAppended;
                },
            },
        },
        .present = {
            .surface    = presentFrame.surface,
            .imageIndex = presentFrame.imageIndex,
        },
    });

    {
        YA_PERF_SCOPE(perf::sample::renderSubmit(), perf::metric::cpuTimeMs(), perf::domain::render());
        if (recorded) {
            submitPresentFrame(presentFrame, {recorded->getHandle()});
        }
        else {
            submitPresentFrame(presentFrame, {});
        }
    }
    app.presentModuleExtras(dt);
}

} // namespace ya
