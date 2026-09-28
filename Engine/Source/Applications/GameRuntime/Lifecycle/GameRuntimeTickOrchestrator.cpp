#include "GameRuntime/Lifecycle/GameRuntimeTickOrchestrator.h"

#include "GameRuntime/App.h"
#include "GameRuntime/HostRenderSettings.h"
#include "GameRuntime/AppRenderState.h"
#include "GameRuntime/Automation/AppAutomationControlService.h"
#include "GameRuntime/Lifecycle/AppAutomation.h"
#include "GameRuntime/Lifecycle/FPSCtrl.h"
#include "Render3D/Services/RenderDiagnosticsService.h"

#include "Core/Async/TaskQueue.h"
#include "Core/Manager/Facade.h"
#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Core/System/FileWatcher.h"

#include "ECS/Component/2D/BillboardComponent.h"
#include "Scene3D/TransformComponent.h"
#include "ECS/Systems/LuaScriptingSystem.h"

#include "RHI/Backend/Vulkan//VulkanRender.h"
#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/PresentFrame.h"
#include "RHI/Core/RenderSurfaceContext.h"
#include "RHI/NativeWindow.h"
#include "RHI/Render.h"
#include "RHI/RenderDefines.h"

#include "Render3D/Common/RenderFrameInputs.h"
#include "Render3D/Common/SceneRenderScheduler.h"
#include "Render3D/Common/RecordedFrame.h"
#include "Render3D/RenderDeviceState.h"
#include "Render3D/Material/Material.h"
#include "GameRuntime/Render/HostSceneExtract.h"
#include "GameRuntime/Render/RenderFrameExtractor.h"
#include "GameRuntime/Render/RuntimeRenderContext.h"
#include "Scene/Core/Scene.h"
#include "Scene/Runtime/SceneManager.h"

#include "Core/Math/Math.h"
#include "ECS/Component.h"
#include "Render3D/Common/CameraFrustumOverlay.h"

#include <algorithm>
#include <format>
#include <functional>
#include <vector>

namespace ya
{

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

    {
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
    reportTickToAutomation(app);

    return 0;
}

/// The tick's report to the automation plane, after the frame completed: which
/// images this tick produced (the displayed View's world images, and the
/// presenting surface's image), and -- when tick automation is enabled -- the
/// capture / diagnostic hooks for this tick. Reporting is a step, not the
/// frame skeleton: `iterate` reads as fps → logic → render → callbacks →
/// tick++ → report.
void GameRuntimeTickOrchestrator::reportTickToAutomation(App& app)
{
    auto& renderServices = app.getRenderServices();
    auto* device         = renderServices.getDeviceState();
    auto* render         = renderServices.getRender();
    // The window this app presents (a bootstrap fact, not a rank): the app
    // names it once, in `AppRenderServices`, and this reads that answer.
    auto* primarySurface = renderServices.getHostSurface();
    const auto presentationImage =
        (device && primarySurface) ? device->getPresentationImageShared(*primarySurface) : nullptr;
    // The three images automation may capture, each named: the displayed View
    // supplies the world images and the primary surface supplies the window's.
    // "The postprocess output" is that View's finalize image when it has one of
    // its own -- when it does not, the View's colour IS the image, which is why
    // the pair is offered and the consumer picks.
    const RenderViewOutput* displayedViewOutput = renderServices.getDisplayedViewOutput();
    const auto              postprocessImage =
        (displayedViewOutput && displayedViewOutput->display && displayedViewOutput->display != displayedViewOutput->color)
            ? displayedViewOutput->display
            : nullptr;
    const auto viewportImage = displayedViewOutput ? displayedViewOutput->color : nullptr;
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
}

/// The logic half of a frame, in contract order. Game pause (`App::isPaused`)
/// stops only Simulation systems and world scripts; everything else keeps the
/// engine, the editor and the UI responsive on a paused frame.
///   task queue, timers, automation          always
///   systems in registration order           Simulation group skipped on pause
///   world Lua (Runtime / Simulation)        skipped on pause
///   file watcher (hot reload), modules      always
///   UI logic                                always (per-tree clock)
///   structural flush (queued destroys)      always
///   input state edges                       always
void GameRuntimeTickOrchestrator::tickLogic(App& app, float dt)
{
    YA_PROFILE_FUNCTION()
    const bool bGamePaused = app.isPaused();
    const bool bPlaying    = app.isRuntimeMode() || app.isSimulationMode();
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
        for (auto& entry : app._systems) {
            if (bGamePaused && entry.group == ESystemTickGroup::Simulation) {
                continue;
            }
            entry.system->onUpdate(dt);
        }
    }

    if (bPlaying && !bGamePaused && app._luaScriptingSystem) {
        YA_PROFILE_SCOPE("Logic/Lua");
        app._luaScriptingSystem->onUpdate(dt);
    }

    if (auto* watcher = FileWatcher::get()) {
        YA_PROFILE_SCOPE("Logic/FileWatcher");
        YA_PERF_SCOPE(perf::sample::appFileWatcher(), perf::metric::cpuTimeMs(), perf::domain::game());
        watcher->poll();
    }

    app.tickModules(dt);
    tickUILogic(app, dt);
    flushStructuralChanges(app);
    {
        YA_PROFILE_SCOPE("Logic/InputPostUpdate");
        app.inputManager.postUpdate();
    }

    {
        YA_PROFILE_SCOPE("Logic/InputPreUpdate");
        app.inputManager.preUpdate();
    }
}

void GameRuntimeTickOrchestrator::tickUILogic(App& app, float dt)
{
    YA_PROFILE_SCOPE("Logic/UI");
    if (!app.isRuntimeMode() && !app.isSimulationMode()) {
        return;
    }
    GameUIHost* gameUIHost = app.getGameUIHost();
    if (!gameUIHost || !gameUIHost->getMountedScene()) {
        return;
    }
    // A paused frame still advances real time; the host decides which clock
    // its tree follows, so a pause menu keeps animating while a HUD holds.
    gameUIHost->update(FUIFrameClock{
        .gameDelta = app.isPaused() ? 0.0f : dt,
        .realDelta = dt,
    });
}

void GameRuntimeTickOrchestrator::flushStructuralChanges(App& app)
{
    YA_PROFILE_SCOPE("Logic/StructuralFlush");
    Scene* scene = app.getSceneServices().getActiveScene();
    if (!scene) {
        return;
    }
    LuaScriptingSystem* lua = app._luaScriptingSystem;
    scene->flushQueuedDestroys([lua](Entity& entity) {
        if (lua) {
            lua->onEntityDestroying(entity);
        }
    });
}

void GameRuntimeTickOrchestrator::prepareHostViewState(App& app, float dt)
{
    (void)dt;

    // The clock is the only host fact this step owns; see HostRenderSettings.h
    // for the writer map. The View the window shows is not host state at all --
    // it is this frame's arrangement, written once in RuntimeRenderContext::tick
    // from the plan.
    HostRenderSettings& hostSettings = app._renderState->hostSettings;
    if (!app.getRenderServices().getDeviceState()) {
        // No renderer: there is no host view this tick, not even a clock.
        hostSettings = {};
        return;
    }

    hostSettings.clock.hostTick      = App::currentHostTick();
    hostSettings.clock.elapsedTimeMS = app.getElapsedTimeMS();
}

void GameRuntimeTickOrchestrator::tickRender(App& app, float dt)
{
    // The frame render context is created with the device and destroyed before
    // it, so "is there a context" is the same question as "is there a renderer
    // this tick" -- and modules are prepared only for a tick that has one.
    auto* context = app._renderState ? app._renderState->runtimeRender.get() : nullptr;
    if (!context) {
        return;
    }

    // App-shell preparation, then the frame itself: the whole order lives in
    // RuntimeRenderContext (see its class comment for the sequence).
    app.prepareModulesForRender(dt);
    prepareHostViewState(app, dt);
    context->tick(app, dt);
}


} // namespace ya
