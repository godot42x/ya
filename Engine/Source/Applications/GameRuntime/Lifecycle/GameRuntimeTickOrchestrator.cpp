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
#include "Render3D/Common/RecordedFrame.h"
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
        YA_PROFILE_SCOPE("Logic/Systems");
        for (auto& sys : app._systems) {
            sys->onUpdate(dt);
        }
    }

    if (app.getSceneServices().getActiveScene()) {
        YA_PROFILE_SCOPE("Logic/RuntimeCamera");
        // The aspect follows the *host surface area this tick is sized for*, not
        // last tick's published View output. Reading the device's published
        // extent made the camera's aspect depend on what was rendered before,
        // which is exactly the "global viewport" this chain is removing: every
        // View declares its own extent, so the only thing the game camera can
        // honestly match is the area the host is giving the views.
        // `syncRuntimeCameraAspect` ignores a degenerate extent.
        const Extent2D viewportExtent = Extent2D::fromVec2(app._renderState->hostView.viewportRect.extent);
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
}

void GameRuntimeTickOrchestrator::pumpOffscreenTasks(App& app, RenderDeviceState* device)
{
    YA_PROFILE_SCOPE("Render/PumpOffscreenTasks");
    // Pre-record prerequisite. Named as a step because it is one: the derived
    // resources this tick's Views bind were produced by offscreen jobs queued on
    // earlier ticks, and reading them before their fence has been waited on is a
    // use-before-ready, not a slower frame.
    pumpOffscreenTasks(app, device);
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

    hostView.clock.hostTick      = App::currentHostTick();
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

    // declare → extract → prepare → build → acquire → record → submit → extras
    declareViews(app, dt, device);
    ExtractedSceneRender sceneRender = extractScenes(app, sceneScheduler, device);
    prepareViews(app, dt, flightIndex, sceneRender);

    TickFrame gameFrame = buildGameRenderFrame(app, dt, flightIndex);

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

    const RecordedFrame recorded = recordFrame(app, *coordinator, dt, std::move(sceneRender), gameFrame, presentFrame);
    submitRecordedFrame(app, presentFrame, recorded);
    app.presentModuleExtras(dt);
}

void GameRuntimeTickOrchestrator::declareViews(App& app, float dt, RenderDeviceState* device)
{
    HostViewState& hostView = app._renderState->hostView;

    // Declare this tick's views. Every owner declares its own (the game
    // viewport, the editor's authoring viewport, the camera preview), so a view
    // exists exactly because somebody asked for it: no layer has to flip a
    // global switch to hide a view another layer declared.
    SceneViewCollector collector;
    auto* scene = app._sceneManager ? app._sceneManager->getActiveScene() : nullptr;
    const SceneViewCollectContext collectContext{
        .activeScene    = scene,
        .viewportRect   = hostView.viewportRect,
        .hostTick       = App::_hostTick,
        .deltaTime      = dt,
    };
    for (ISceneViewProducer* producer : app._renderState->viewProducers) {
        if (producer) {
            producer->collectSceneViews(collectContext, collector);
        }
    }

    // submit() stores a copy, so from here on the declarations live in the
    // scheduler's frame and this collector has no readers.
    auto& sceneScheduler = app._renderState->sceneRenderScheduler;
    for (const SceneViewDesc& view : collector.views()) {
        (void)sceneScheduler.submit(view);
    }

    // The host viewport's camera is what the host reports as "the world view":
    // the camera packet and the offscreen extent follow the declaration instead
    // of an injected copy of it. Which declaration that is comes from the same
    // structural predicate the plan uses for its display root -- matching a
    // well-known view id here would be a second definition of "the host view"
    // that can disagree with it.
    for (const SceneViewDesc& view : collector.views()) {
        if (!view.ownsHostViewport()) {
            continue;
        }
        // The declared rect is the host view's geometry, so the host's copy and
        // the extent the device expects follow the declaration instead of a rect
        // the owner pushed into host state.
        hostView.view       = view.view;
        hostView.projection = view.projection;
        hostView.cameraPos  = view.cameraPos;
        hostView.viewportRect = view.viewportRect;
        device->applyViewportResize(view.viewportRect);
        break;
    }
}

ExtractedSceneRender GameRuntimeTickOrchestrator::extractScenes(App&                 app,
                                                               SceneRenderScheduler& scheduler,
                                                               RenderDeviceState*   device)
{
    // Extraction is its own step: seal() only grouped the declarations, so
    // Scene/ECS content is read here and nowhere earlier.
    ExtractedSceneRender sceneRender =
        extractHostSceneSnapshots(scheduler.seal(), device->getTerrainProcessor());

    // Animation policy input: poses are consumed by the world pipeline, so the
    // honest question is "did the renderer produce content for this Scene", not
    // "is some viewport's world switch on". One tick of lag by construction --
    // systems run before views are declared.
    app._renderState->renderedScenesLastTick = renderedScenes(sceneRender.plan());
    return sceneRender;
}

void GameRuntimeTickOrchestrator::prepareViews(App&                  app,
                                              float                 dt,
                                              uint32_t              flightIndex,
                                              ExtractedSceneRender& sceneRender)
{
    auto& viewFrames = app._renderState->viewFrameDataPerFlight[flightIndex];
    sceneRender.pairViewFrames(viewFrames);
    if (sceneRender.empty()) {
        return;
    }

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
                .elapsedTimeSeconds = app._renderState->hostView.clock.elapsedTimeMS / 1000.0f,
                .shadowSettings = &app.getRenderServices().getShadowSettings(),
            },
            sceneRender.snapshotFor(task),
            frameData);
    }
}

GameRuntimeTickOrchestrator::TickFrame GameRuntimeTickOrchestrator::buildGameRenderFrame(App&     app,
                                                                                         float    dt,
                                                                                         uint32_t flightIndex)
{
    const HostViewState& hostView = app._renderState->hostView;

    TickFrame tickFrame;
    // Frame-level only: which cameras draw and where their outputs go is on
    // each View's declaration and prepared data. The host contributes the
    // tick's clock and its render scale.
    tickFrame.frame = FramePacket{
        .flightIndex   = flightIndex,
        .frameIndex    = App::_hostTick,
        .deltaTime     = dt,
        .viewportFrameBufferScale = hostView.viewportFrameBufferScale,
        .shadowSettings = &app.getRenderServices().getShadowSettings(),
    };

    // Game UI: build the immutable frame snapshot BEFORE the RenderGraph.
    // Command recording consumes only this packet; the live WidgetTree is
    // never touched while recording. Runtime/simulation only (standalone game
    // and PIE); the editor's 3D authoring viewport has no game UI.
    const Scene* scene = app._sceneManager ? app._sceneManager->getActiveScene() : nullptr;
    if ((app.isRuntimeMode() || app.isSimulationMode()) && scene) {
        if (auto* gameUIHost = app.getGameUIHost()) {
            gameUIHost->setPresentation(hostView.viewportRect,
                                        glm::vec2(tickFrame.frame.viewportFrameBufferScale));
            tickFrame.uiSnapshot = gameUIHost->buildSnapshot();
        }
    }
    return tickFrame;
}

RecordedFrame GameRuntimeTickOrchestrator::recordFrame(App&                    app,
                                                       RenderFrameCoordinator& coordinator,
                                                       float                   dt,
                                                       ExtractedSceneRender    sceneRender,
                                                       TickFrame&              frame,
                                                       const FPresentFrame&    presentFrame)
{
    return coordinator.record(RenderFramePlan{
        .sceneRender = std::move(sceneRender),
        .frame = frame.boundFrame(),
        .viewCompose = {
            // Empty: the host's View-inset list. Overlay recording is a stage of
            // `recordExtensions`, not data on the plan.
        },
        .present = {
            .surface    = presentFrame.surface,
            .imageIndex = presentFrame.imageIndex,
        },
        .recordExtensions = &app,
    });
}

void GameRuntimeTickOrchestrator::submitRecordedFrame(App&                 app,
                                                      FPresentFrame&       presentFrame,
                                                      const RecordedFrame& recorded)
{
    YA_PERF_SCOPE(perf::sample::renderSubmit(), perf::metric::cpuTimeMs(), perf::domain::render());
    // The host submits what the renderer recorded, or an empty frame when the
    // recording was refused; the image is presented either way.
    // YA_CORE_TRACE("Submit tick {}: flight={} token={} recorded={}",
    //               app.getHostTick(),
    //               recorded.flightIndex,
    //               recorded.frameToken,
    //               recorded.valid());
    submitPresentFrame(presentFrame,
                       recorded.valid() ? std::vector<void*>{recorded.commandBuffer->getHandle()}
                                        : std::vector<void*>{});
}

} // namespace ya
