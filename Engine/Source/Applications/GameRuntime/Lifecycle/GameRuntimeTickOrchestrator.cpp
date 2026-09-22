#include "GameRuntime/Lifecycle/GameRuntimeTickOrchestrator.h"

#include "GameRuntime/App.h"
#include "GameRuntime/HostRenderSettings.h"
#include "GameRuntime/AppRenderState.h"
#include "GameRuntime/Automation/AppAutomationControlService.h"
#include "GameRuntime/Lifecycle/AppAutomation.h"
#include "HostSdlEventSource.h"
#include "GameRuntime/Lifecycle/FPSCtrl.h"
#include "GameRuntime/Render/SceneCameraQuery.h"
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
#include "Render3D/Common/SceneRenderScheduler.h"
#include "Render3D/Common/RecordedFrame.h"
#include "Render3D/RenderDeviceState.h"
#include "Render3D/Material/Material.h"
#include "GameRuntime/Render/HostSceneExtract.h"
#include "GameRuntime/Render/RenderFrameExtractor.h"
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
void syncRuntimeCameraAspect(Entity* runtimeCamera, const Extent2D& viewExtent)
{
    if (!runtimeCamera || !runtimeCamera->isValid() || !runtimeCamera->hasComponent<CameraComponent>()) {
        return;
    }
    if (viewExtent.width == 0 || viewExtent.height == 0) {
        return;
    }

    auto* camera = runtimeCamera->getComponent<CameraComponent>();
    if (!camera->_fixedAspectRatio) {
        camera->setAspectRatio(static_cast<float>(viewExtent.width) /
                               static_cast<float>(viewExtent.height));
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
    auto* render         = renderServices.getRender();
    auto* primarySurface = render ? render->getPrimarySurfaceContext() : nullptr;
    const auto presentationImage =
        (device && primarySurface) ? device->getPresentationImageShared(*primarySurface) : nullptr;
    // The three images automation may capture, each named: the host viewport's
    // View supplies the world images and the primary surface supplies the
    // window's. "The postprocess output" is that View's finalize image when it
    // has one of its own -- when it does not, the View's colour IS the image,
    // which is why the pair is offered and the consumer picks.
    const RenderViewOutput* hostViewport = renderServices.getHostViewportOutput();
    const auto              postprocessImage =
        (hostViewport && hostViewport->display && hostViewport->display != hostViewport->color)
            ? hostViewport->display
            : nullptr;
    const auto viewportImage = hostViewport ? hostViewport->color : nullptr;
    if (auto* automationControl = app.getAutomationControlService()) {
        automationControl->onTickCompleted(app,
                                            renderServices.getRender(),
                                            postprocessImage,
                                            viewportImage,
                                            presentationImage,
                                            App::_hostTick);
    }

    if (AppAutomation::isTickAutomationEnabled(app)) {
        YA_PROFILE_SCOPE("Tick/Automation");
        YA_PERF_SCOPE(perf::sample::tickAutomation(), perf::metric::cpuTimeMs(), perf::domain::render());
        auto* diagnosticsService = device ? &device->getDiagnosticsService() : nullptr;

        AppAutomation::onTickCompleted(app,
                                        AppAutomationTickContext{
                                            .render                     = renderServices.getRender(),
                                            .postprocessImage           = postprocessImage,
                                            .viewportImage              = viewportImage,
                                            .presentationImage          = presentationImage,
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
        // The game camera's aspect follows the resolution its viewport renders
        // at: a setting this tick reads directly. Not last tick's published View
        // output (that made the aspect depend on what was rendered before), and
        // not the window (the window only decides how the image is presented, so
        // following it would change what is rendered on a resize that changes
        // nothing about the image).
        // `syncRuntimeCameraAspect` ignores a degenerate extent.
        syncRuntimeCameraAspect(findPrimaryCamera(*app.getSceneServices().getActiveScene()),
                               app._renderState->hostSettings.renderResolution);
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
    device->getOffscreenTaskService().tick(app.getTaskManager());
}


void GameRuntimeTickOrchestrator::prepareHostViewState(App& app, float dt)
{
    (void)dt;

    // The clock is the only host fact this step owns; see HostRenderSettings.h
    // for the writer map. The View the window shows is not host state at all --
    // it is this frame's arrangement, written once in tickRender from the plan.
    HostRenderSettings& hostSettings = app._renderState->hostSettings;
    if (!app.getRenderServices().getDeviceState()) {
        // No renderer: there is no host view this tick, not even a clock.
        hostSettings = {};
        return;
    }

    hostSettings.clock.hostTick      = App::currentHostTick();
    hostSettings.clock.elapsedTimeMS = app.getElapsedTimeMS();
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
    auto* device = app.getRenderServices().getDeviceState();
    if (!device) {
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

    // The one call that makes the previous paragraph true. It used to be
    // inlined here while `pumpOffscreenTasks` called only itself, so the named
    // step existed in the header and as an infinite recursion, and the real call
    // was anonymous. One step, one body, one call site.
    pumpOffscreenTasks(app, device);

    const uint32_t flightIndex = resolveFlightIndex(app);

    // This tick's declaration collector and the plan sealed from it. It is a
    // local, not host state: `beginTick -> submit -> seal` describes one tick's
    // arrangement, so its lifetime is this scope, and a scheduler that outlived
    // the tick could only hold declarations the tick already resolved.
    SceneRenderScheduler sceneScheduler;
    sceneScheduler.beginTick(App::_hostTick);

    // declare → extract → prepare → build → acquire → record → submit → extras
    declareViews(app, dt, sceneScheduler);
    ExtractedSceneRender sceneRender = extractScenes(app, sceneScheduler, device);
    prepareViews(app, dt, flightIndex, sceneRender);

    TickFrame gameFrame = buildGameRenderFrame(app, dt, flightIndex, sceneRender);

    // The app's arrangement for this frame: which View the host window shows, in
    // which flight its output is published, and the camera it renders from.
    // Written once, here, from the plan -- before the recording below, because the
    // editor's compose and chrome stages run *inside* it and read this View. The
    // renderer never learns it: it publishes every View and names none of them
    // "the current one". Which declaration is the host's comes from the same
    // structural predicate the plan uses for its display root, so a well-known
    // view id here would be a second definition that can disagree with it.
    {
        const SceneViewTask* displayRoot = sceneRender.displayRootTask();
        app._renderState->hostViewport = HostViewportView{
            .viewId      = displayRoot ? displayRoot->desc.viewId : 0,
            .flightIndex = flightIndex,
            .view        = displayRoot ? displayRoot->desc.view : glm::mat4(1.0f),
            .projection  = displayRoot ? displayRoot->desc.projection : glm::mat4(1.0f),
            .cameraPos   = displayRoot ? displayRoot->desc.cameraPos : glm::vec3(0.0f),
        };
    }

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

    const RecordedFrame recorded = recordFrame(app, *device, dt, std::move(sceneRender), gameFrame, presentFrame);
    submitRecordedFrame(app, presentFrame, recorded);
    app.presentModuleExtras(dt);
}

void GameRuntimeTickOrchestrator::declareViews(App&                  app,
                                               float                 dt,
                                               SceneRenderScheduler& scheduler)
{
    const HostRenderSettings& hostSettings = app._renderState->hostSettings;

    // Declare this tick's views. Every owner declares its own (the game
    // viewport, the editor's authoring viewport, the camera preview), so a view
    // exists exactly because somebody asked for it: no layer has to flip a
    // global switch to hide a view another layer declared.
    SceneViewCollector collector;
    auto* scene = app._sceneManager ? app._sceneManager->getActiveScene() : nullptr;
    const SceneViewCollectContext collectContext{
        .activeScene    = scene,
        .renderResolution = hostSettings.renderResolution,
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
    for (const SceneViewDesc& view : collector.views()) {
        (void)scheduler.submit(view);
    }

    // This step declares and submits, and adopts nothing: which of these Views
    // the host window shows, and its camera, is read off the sealed plan once in
    // tickRender (see the hostViewport assignment there). Adopting it here meant
    // two writers for one fact, and a reader had to know which of them had run.
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
        const SceneViewTask& task      = *recording.task;
        const SceneViewDesc&     desc      = task.desc;
        RenderFrameData&         frameData = *recording.frameData;
        RenderFrameExtractor::prepareView(
            RenderFrameExtractor::ViewPrepareInput{
                .view = desc.view,
                .projection = desc.projection,
                .viewProjection = desc.viewProjection(),
                .cameraPos = desc.cameraPos,
                .viewExtent = Extent2D::fromVec2(desc.outputRect.extent),
                .viewOwner = desc.viewOwner,
                .viewFeatures = desc.features,
                .frameIndex = App::_hostTick,
                .deltaTime = dt,
                .elapsedTimeSeconds = app._renderState->hostSettings.clock.elapsedTimeMS / 1000.0f,
                .shadowSettings = &app.getRenderServices().getShadowSettings(),
            },
            sceneRender.snapshotFor(task),
            frameData);
    }
}

GameRuntimeTickOrchestrator::TickFrame GameRuntimeTickOrchestrator::buildGameRenderFrame(
    App&                        app,
    float                       dt,
    uint32_t                    flightIndex,
    const ExtractedSceneRender& sceneRender)
{
    const HostRenderSettings& hostSettings = app._renderState->hostSettings;

    TickFrame tickFrame;
    // Frame-level only: which cameras draw and where their outputs go is on
    // each View's declaration and prepared data. The host contributes the
    // tick's clock and its render scale.
    tickFrame.frame = FramePacket{
        .flightIndex   = flightIndex,
        .frameIndex    = App::_hostTick,
        .deltaTime     = dt,
        .renderScale = hostSettings.renderScale,
        .shadowSettings = &app.getRenderServices().getShadowSettings(),
    };

    // Game UI: build the immutable frame snapshot BEFORE the RenderGraph.
    // Command recording consumes only this packet; the live WidgetTree is
    // never touched while recording. Runtime/simulation only (standalone game
    // and PIE); the editor's 3D authoring viewport has no game UI.
    //
    // The UI is composed onto the host viewport's View, so its logical viewport
    // is that View's declared rect. Deriving it from the plan rather than from a
    // host copy is what makes PIE correct: there the host viewport is the
    // editor's authoring panel, not the game's render resolution. A tick that
    // declares no host viewport presents no Game UI, so the tree keeps the size
    // it was last presented at instead of being resized to a target that does not
    // exist.
    const Scene* scene = app._sceneManager ? app._sceneManager->getActiveScene() : nullptr;
    const SceneViewTask* displayRoot = sceneRender.displayRootTask();
    if ((app.isRuntimeMode() || app.isSimulationMode()) && scene && displayRoot) {
        if (auto* gameUIHost = app.getGameUIHost()) {
            gameUIHost->setPresentation(displayRoot->desc.outputRect,
                                        glm::vec2(tickFrame.frame.renderScale));
            tickFrame.uiSnapshot = gameUIHost->buildSnapshot();
        }
    }
    return tickFrame;
}

RecordedFrame GameRuntimeTickOrchestrator::recordFrame(App&                    app,
                                                       RenderDeviceState&      device,
                                                       float                   dt,
                                                       ExtractedSceneRender    sceneRender,
                                                       TickFrame&              frame,
                                                       const FPresentFrame&    presentFrame)
{
    return device.record(RenderFramePlan{
        .sceneRender = std::move(sceneRender),
        .frame = frame.boundFrame(),
        .viewCompose = {
            // Empty: the host's View-inset list. Overlay recording is a stage of
            // `recordExtensions`, not data on the plan.
        },
        .present = {
            .surface    = presentFrame.surface,
            .imageIndex = presentFrame.imageIndex,
            // What the window shows is a host fact, so the host declares it
            // rather than leaving the surface pass to infer it from a View's
            // compose structure. The module that fills the surface (the editor
            // chrome) is what answers; a standalone runtime has none, so the
            // surface is the View.
            .backdrop = app.presentsViewDisplayImage() ? ESurfaceBackdrop::ViewDisplayImage
                                                       : ESurfaceBackdrop::HostContent,
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
