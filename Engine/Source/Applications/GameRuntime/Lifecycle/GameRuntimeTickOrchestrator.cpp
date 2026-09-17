#include "GameRuntime/Lifecycle/GameRuntimeTickOrchestrator.h"

#include "GameRuntime/App.h"
#include "GameRuntime/AppRenderFrameState.h"
#include "GameRuntime/AppRenderState.h"
#include "GameRuntime/Automation/AppAutomationControlService.h"
#include "GameRuntime/Lifecycle/AppAutomation.h"
#include "HostSdlEventSource.h"
#include "GameRuntime/Utility/FPSCtrl.h"
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
#include "GameRuntime/Lifecycle/HostSceneRenderSubmit.h"
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

uint64_t entityUUID(Entity* entity)
{
    if (!entity || !entity->isValid() || !entity->hasComponent<IDComponent>()) {
        return 0;
    }
    return entity->getComponent<IDComponent>()->_id.value;
}

Entity* findNonPrimarySceneCamera(Scene& scene, Entity* primaryCamera)
{
    auto& registry = scene.getRegistry();
    for (const auto& [handle, cameraComp] : registry.view<CameraComponent>().each()) {
        (void)cameraComp;
        Entity* entity = scene.getEntityByEnttID(handle);
        if (!entity || entity == primaryCamera) {
            continue;
        }
        return entity;
    }
    return nullptr;
}

Entity* resolvePreviewCamera(Scene& scene, Entity* primaryCamera, bool bHostOwned, uint64_t previewEntityUUID)
{
    if (bHostOwned) {
        if (previewEntityUUID == 0) {
            return nullptr;
        }
        Entity* selected = scene.getEntityByUUID(previewEntityUUID);
        if (!selected || !selected->isValid() || !selected->hasComponent<CameraComponent>() ||
            !selected->hasComponent<TransformComponent>()) {
            return nullptr;
        }
        return selected;
    }
    return findNonPrimarySceneCamera(scene, primaryCamera);
}

glm::mat4 cameraProjectionForOutput(const CameraComponent& camera, const glm::vec2& outputExtent)
{
    if (camera._fixedAspectRatio || outputExtent.x <= 0.0f || outputExtent.y <= 0.0f) {
        return camera.getProjection();
    }
    return FMath::perspective(glm::radians(camera._fov),
                              outputExtent.x / outputExtent.y,
                              camera._nearClip,
                              camera._farClip);
}

constexpr glm::vec4 kSelectedCameraFrustumColor = {1.0f, 0.85f, 0.2f, 1.0f};
constexpr SceneViewId kHostOverlayPreviewViewId = 2;

// Compact FOV wireframe for the host-selected camera only. The camera body is a
// world-space mesh (CameraMeshLinkageRule); these lines stay procedural so they
// can follow FOV without a new pipeline.
void appendSceneCameraFrustumLines(Scene&                            scene,
                                   Entity*                           primaryCamera,
                                   uint64_t                          previewEntityUUID,
                                   std::vector<RenderOverlayLine3D>& lines)
{
    if (previewEntityUUID == 0) {
        return;
    }

    auto& registry = scene.getRegistry();
    for (const auto& [handle, cameraComp] : registry.view<CameraComponent>().each()) {
        Entity* entity = scene.getEntityByEnttID(handle);
        if (!entity || !entity->isValid() || entity == primaryCamera ||
            !entity->hasComponent<TransformComponent>()) {
            continue;
        }
        if (entityUUID(entity) != previewEntityUUID) {
            continue;
        }
        appendCameraFrustumOverlayLines(lines,
                                        cameraComp.getFreeView(),
                                        cameraComp.getProjection(),
                                        kSelectedCameraFrustumColor);
    }
}

} // namespace

int GameRuntimeTickOrchestrator::iterate(App& app, float dt)
{
    YA_PROFILE_FUNCTION()
    YA_PERF_FUNCTION(perf::metric::cpuTimeMs(), perf::domain::render());

    YA_PERF_FRAME_SCOPE(
        perf::sample::renderFrame(),
        perf::metric::cpuTimeMs(),
        perf::domain::render(),
        perf::sample::frameUnaccounted(),
        perf::sample::frameEventPump(),
        perf::sample::frameFpsControl(),
        perf::sample::frameLogic(),
        perf::sample::frameRender(),
        perf::sample::frameMainThreadCallbacks(),
        perf::sample::frameAutomation());

    {
        YA_PROFILE_SCOPE("Frame/FpsControl");
        YA_PERF_SCOPE(perf::sample::frameFpsControl(), perf::metric::cpuTimeMs(), perf::domain::game());
        dt += FPSControl::get()->update(dt);
    }

    if (!app._bPause) {
        YA_PROFILE_SCOPE("Frame/Logic");
        YA_PERF_SCOPE(perf::sample::frameLogic(), perf::metric::cpuTimeMs(), perf::domain::game());
        tickLogic(app, dt);
    }
    {
        YA_PROFILE_SCOPE("Frame/Render");
        YA_PERF_SCOPE(perf::sample::frameRender(), perf::metric::cpuTimeMs(), perf::domain::render());
        tickRender(app, dt);
    }
    {
        YA_PROFILE_SCOPE("Frame/MainThreadCallbacks");
        YA_PERF_SCOPE(perf::sample::frameMainThreadCallbacks(), perf::metric::cpuTimeMs(), perf::domain::game());
        YA_PERF_SCOPE(perf::sample::frameRenderCallbacks(), perf::metric::cpuTimeMs(), perf::domain::render());
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

    if (AppAutomation::isFrameAutomationEnabled(app)) {
        YA_PROFILE_SCOPE("Frame/Automation");
        YA_PERF_SCOPE(perf::sample::frameAutomation(), perf::metric::cpuTimeMs(), perf::domain::render());
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
                                                              app._renderState->frameState.viewportRect);
        syncRuntimeCameraAspect(getPrimaryCamera(app), viewportExtent);
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

Entity* GameRuntimeTickOrchestrator::getPrimaryCamera(const App& app)
{
    if (!app._sceneManager) {
        return nullptr;
    }

    Scene* scene = app._sceneManager->getActiveScene();
    if (!scene || !scene->isValid()) {
        return nullptr;
    }

    auto& registry = scene->getRegistry();

    Entity* anyCam = nullptr;
    for (const auto& [entity, cameraComp] : registry.view<CameraComponent>().each()) {
        if (cameraComp.bPrimary) {
            return scene->getEntityByEnttID(entity);
        }
        anyCam = scene->getEntityByEnttID(entity);
    }

    return anyCam;
}

void GameRuntimeTickOrchestrator::prepareHostViewState(App& app, float dt)
{
    auto* device = app.getRenderServices().getDeviceState();
    if (!device) {
        app._renderState->frameState = {};
        return;
    }

    app._renderState->frameState.clock.hostTick    = App::_hostTick;
    app._renderState->frameState.clock.elapsedTimeMS = app.getElapsedTimeMS();

    Rect2D viewportRect = app._renderState->frameState.viewportRect;
    if (viewportRect.extent.x <= 0.0f || viewportRect.extent.y <= 0.0f) {
        viewportRect = Rect2D{
            .pos    = {0.0f, 0.0f},
            .extent = app._windowSize,
        };
    }

    (void)dt;

    Entity* runtimeCamera = getPrimaryCamera(app);

    const bool bUseRuntimeCamera = app._appState == AppState::Runtime &&
                                   runtimeCamera && runtimeCamera->isValid() &&
                                   runtimeCamera->hasComponent<CameraComponent>();

    const float viewportFrameBufferScale = app._renderState->frameState.viewportFrameBufferScale;
    const HostClockState clock  = app._renderState->frameState.clock;

    AppRenderFrameState frameState{};
    frameState.clock                    = clock;
    frameState.viewportRect             = viewportRect;
    frameState.viewportFrameBufferScale = viewportFrameBufferScale;
    if (bUseRuntimeCamera) {
        auto cc                      = runtimeCamera->getComponent<CameraComponent>();
        auto tc                      = runtimeCamera->getComponent<TransformComponent>();
        frameState.view              = cc->getFreeView();
        frameState.projection        = cc->getProjection();
        frameState.cameraPos         = tc->getWorldPosition();
        app._renderState->frameState = frameState;
        return;
    }

    if (app._renderState->extensionFrameState) {
        frameState.view       = app._renderState->extensionFrameState->view;
        frameState.projection = app._renderState->extensionFrameState->projection;
        frameState.cameraPos  = app._renderState->extensionFrameState->cameraPos;
    }
    app._renderState->frameState = frameState;
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
    // The extracted snapshot (draw items / lights / skinning palettes) is only
    // consumed by the world pipeline; 2D canvas mode disables the world scene
    // graph, so extraction would be pure waste. Drop the stale per-flight
    // snapshot instead, mirroring the world output handling in
    // getViewportDisplayImageShared.
    const auto& frameState = app._renderState->frameState;
    const glm::mat4 viewProjection = makeCameraViewProjection(frameState.projection, frameState.view);

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

    SceneRenderPlan sceneRenderPlan;
    std::vector<RenderOverlayLine3D> cameraFrustumLines;
    std::vector<HostSceneViewSubmit> hostViews;
    Entity* runtimeLookCamera = nullptr;
    Entity* previewCamera     = nullptr;
    if (app.getRenderServices().isWorldSceneRenderEnabled() && scene) {
        hostViews.push_back(HostSceneViewSubmit{
            .scene        = scene,
            .viewId       = kPrimarySceneViewId,
            .view         = frameState.view,
            .projection   = frameState.projection,
            .cameraPos    = frameState.cameraPos,
            .viewportRect = frameState.viewportRect,
        });

        runtimeLookCamera = (app._appState == AppState::Runtime) ? getPrimaryCamera(app) : nullptr;
        previewCamera     = resolvePreviewCamera(*scene,
                                                         runtimeLookCamera,
                                                         app._renderState->bCameraPreviewHostOwned,
                                                         app._renderState->cameraPreviewEntityUUID);
        if (previewCamera && previewCamera == runtimeLookCamera) {
            previewCamera = nullptr;
        }
        const Rect2D previewComposeRect = previewCamera
                                              ? makeViewDisplayInsetRect(frameState.viewportRect.extent)
                                              : Rect2D{};
        if (previewCamera && previewComposeRect.extent.x > 0.0f && previewComposeRect.extent.y > 0.0f) {
            auto* cameraComp = previewCamera->getComponent<CameraComponent>();
            auto* transform  = previewCamera->getComponent<TransformComponent>();
            const Rect2D previewOutput{
                .pos    = {0.0f, 0.0f},
                .extent = previewComposeRect.extent,
            };
            hostViews.push_back(HostSceneViewSubmit{
                .scene             = scene,
                .viewId            = kHostOverlayPreviewViewId,
                .view              = cameraComp->getFreeView(),
                .projection        = cameraProjectionForOutput(*cameraComp, previewOutput.extent),
                .cameraPos         = transform->getWorldPosition(),
                .viewportRect      = previewOutput,
                .composeOntoViewId = kPrimarySceneViewId,
                .composeRect       = previewComposeRect,
            });
        }

        appendSceneCameraFrustumLines(*scene,
                                      runtimeLookCamera,
                                      app._renderState->bCameraPreviewHostOwned
                                          ? app._renderState->cameraPreviewEntityUUID
                                          : 0,
                                      cameraFrustumLines);
    }
    (void)submitHostSceneViews(sceneScheduler, device->getTerrainProcessor(), hostViews);
    sceneRenderPlan = sceneScheduler.seal();

    // View visibility is policy, decided here and nowhere else: the editor
    // world view draws generated editor companions, a camera preview (what a
    // camera sees) does not, and the global debug toggle overrides both.
    const FRenderFeatureMask gizmoFeature = toMask(ERenderFeature::Gizmo);
    const FRenderFeatureMask baseFeatures = toMask(ERenderFeature::Game);
    const FRenderFeatureMask editorViewFeatures =
        baseFeatures | ((app.isStopped() || app._renderState->bShowEditorGizmos) ? gizmoFeature : 0u);
    const FRenderFeatureMask previewViewFeatures =
        baseFeatures | (app._renderState->bShowEditorGizmos ? gizmoFeature : 0u);
    const auto featuresForView = [&](SceneViewId viewId) {
        return viewId == kHostOverlayPreviewViewId ? previewViewFeatures : editorViewFeatures;
    };

    auto& viewFrames = app._renderState->viewFrameDataPerFlight[flightIndex];
    std::vector<SceneViewRecording> viewRecordings;
    if (!sceneRenderPlan.viewportTasks.empty()) {
        YA_PERF_SCOPE(perf::sample::renderExtract(), perf::metric::cpuTimeMs(), perf::domain::render());
        YA_PROFILE_SCOPE("RenderFrameExtractor::sceneSnapshot");
        viewFrames.resize(sceneRenderPlan.viewportTasks.size());
        viewRecordings.reserve(viewFrames.size());
        for (size_t index = 0; index < sceneRenderPlan.viewportTasks.size(); ++index) {
            const SceneViewportTask& task = sceneRenderPlan.viewportTasks[index];
            RenderFrameData& frameData = viewFrames[index];
            const auto sceneSnapshot = sceneRenderPlan.snapshotFor(task);
            if (!sceneSnapshot) {
                frameData.clear();
            }
            else {
                RenderFrameExtractor::prepareView(
                    RenderFrameExtractor::ViewPrepareInput{
                        .view = task.view,
                        .projection = task.projection,
                        .viewProjection = task.viewProjection,
                        .cameraPos = task.cameraPos,
                        .viewportExtent = Extent2D::fromVec2(task.viewportRect.extent),
                        .viewOwner = (task.viewId == kHostOverlayPreviewViewId && previewCamera)
                                         ? previewCamera->getHandle()
                                         : (runtimeLookCamera ? runtimeLookCamera->getHandle() : entt::null),
                        .viewFeatures = featuresForView(task.viewId),
                        .frameIndex = App::_hostTick,
                        .deltaTime = dt,
                        .shadowSettings = &app.getRenderServices().getShadowSettings(),
                    },
                    sceneSnapshot,
                    frameData);
            }
            viewRecordings.push_back(SceneViewRecording{
                .task         = &task,
                .frameData    = &frameData,
                .derivedScene = derivedSceneForHostView(hostViews, task),
            });
        }
    }
    else {
        viewFrames.resize(1);
        viewFrames[0].clear();
    }

    CameraFrameInput cameraFrame{
        .flightIndex              = flightIndex,
        .frameIndex               = App::_hostTick,
        .deltaTime                = dt,
        .viewFeatures             = editorViewFeatures,
        .view                     = frameState.view,
        .projection               = frameState.projection,
        .viewProjection           = viewProjection,
        .cameraPos                = frameState.cameraPos,
        .viewportRect             = frameState.viewportRect,
        .viewportFrameBufferScale = frameState.viewportFrameBufferScale,
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
        .worldLines    = &cameraFrustumLines,
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
        .sceneRender = {
            .plan  = viewRecordings.empty() ? nullptr : &sceneRenderPlan,
            .views = std::move(viewRecordings),
        },
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
