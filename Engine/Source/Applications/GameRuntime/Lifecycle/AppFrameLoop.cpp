#include "GameRuntime/Lifecycle/AppFrameLoop.h"

#include "GameRuntime/App.h"
#include "GameRuntime/AppRenderFrameState.h"
#include "GameRuntime/AppRenderState.h"
#include "GameRuntime/Automation/AppAutomationControlService.h"
#include "GameRuntime/Lifecycle/AppAutomation.h"
#include "GameRuntime/Utility/FPSCtrl.h"
#include "Render3D/Services/RenderDiagnosticsService.h"

#include "Core/Async/TaskQueue.h"
#include "App/Kernel/AppKernel.h"
#include "Core/Manager/Facade.h"
#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Core/System/FileWatcher.h"

#include "ECS/Component/2D/BillboardComponent.h"
#include "ECS/Systems/Components/CameraComponent.h"
#include "Scene3D/TransformComponent.h"
#include "ECS/Systems/LuaScriptingSystem.h"

#include "RHI/Backend/Vulkan//VulkanRender.h"
#include "RHI/NativeWindow.h"

#include "GUI/Draw2D/Render2D.h"
#include "Render3D/Material/Material.h"

#include "GameRuntime/Utility/RenderFrameExtractor.h"
#include "GameRuntime/Utility/SDLMisc.h"

#include "Scene/Core/Scene.h"
#include "Scene/Runtime/SceneManager.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <format>

namespace ya
{

namespace
{

Extent2D resolveRuntimeViewportExtent(App& app)
{
    auto* renderRuntime = app.getRenderServices().getRenderRuntime();
    return AppFrameLoop::resolveViewportExtent(app,
                                               renderRuntime,
                                               renderRuntime ? renderRuntime->getViewportRect() : Rect2D{});
}

void syncRuntimeCameraAspect(Scene& scene, const Extent2D& viewportExtent)
{
    if (viewportExtent.width == 0 || viewportExtent.height == 0) {
        return;
    }

    const float aspectRatio = static_cast<float>(viewportExtent.width) / static_cast<float>(viewportExtent.height);
    auto        cameras     = scene.getRegistry().view<CameraComponent>();
    for (auto entityHandle : cameras) {
        auto& camera = cameras.get<CameraComponent>(entityHandle);
        if (!camera._fixedAspectRatio) {
            camera.setAspectRatio(aspectRatio);
        }
    }
}

} // namespace

namespace
{

class HostSdlEventSource final : public IAppEventSource
{
  public:
    void pollEvents(const std::function<void(const Event&)>& emit) override
    {
        SDL_Event event;
        YA_PROFILE_SCOPE("Frame/EventPump");
        YA_PERF_SCOPE(perf::sample::frameEventPump(), perf::metric::cpuTimeMs(), perf::domain::game());
        while (SDL_PollEvent(&event)) {
            processSDLEvent(
                event,
                [&emit](const auto& translated) {
                    emit(translated);
                });
        }
    }
};

class GameRuntimeLoopDelegate final : public IAppLoopDelegate
{
  public:
    explicit GameRuntimeLoopDelegate(App& inApp)
        : app(inApp)
    {
    }

    void onInit() override
    {
    }

    void onEvent(const Event& event) override
    {
        app.dispatchEvent(event);
    }

    void onTick(float dt) override
    {
        AppFrameLoop::iterate(app, dt, /*bPumpNativeEvents=*/false);
    }

    void onShutdown() override {}

    [[nodiscard]] bool shouldClose() const override
    {
        return !app.isRunning();
    }

  private:
    App& app;
};

} // namespace

int App::run()
{
    // AppKernel is the only native while-loop. The product automation layer
    // still owns scene-stability / screenshot / RenderDoc completion and
    // therefore requests App::requestQuit() itself; do not arm the kernel's
    // basic exit-after-frame policy in parallel during this transition.
    _startTime = std::chrono::steady_clock::now();
    _lastTime  = _startTime;

    HostSdlEventSource      eventSource;
    GameRuntimeLoopDelegate delegate(*this);
    AppKernel               kernel({.eventSource = &eventSource}, delegate);
    return kernel.run();
}

void App::tickLogic(float dt)
{
    AppFrameLoop::tickLogic(*this, dt);
}

void App::syncViewportState()
{
    AppFrameLoop::syncViewportState(*this);
}

Extent2D App::resolveViewportExtent(RenderRuntime* renderRuntime, const Rect2D& viewportRect) const
{
    return AppFrameLoop::resolveViewportExtent(*this, renderRuntime, viewportRect);
}

void App::prepareRenderFrameState(float dt)
{
    AppFrameLoop::prepareRenderFrameState(*this, dt);
}

void App::tickRender(float dt)
{
    AppFrameLoop::tickRender(*this, dt);
}

int AppFrameLoop::iterate(App& app, float dt, bool bPumpNativeEvents)
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

    if (bPumpNativeEvents) {
        HostSdlEventSource eventSource;
        eventSource.pollEvents([&app](const Event& event) {
            app.dispatchEvent(event);
        });
    }

    {
        YA_PROFILE_SCOPE("Frame/FpsControl");
        YA_PERF_SCOPE(perf::sample::frameFpsControl(), perf::metric::cpuTimeMs(), perf::domain::game());
        dt += FPSControl::get()->update(dt);
    }

    if (app._bMinimized) {
        SDL_Delay(100);
        return 0;
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
    ++App::_frameIndex;

    auto& renderServices = app.getRenderServices();
    auto* renderRuntime  = renderServices.getRenderRuntime();
    if (auto* automationControl = app.getAutomationControlService()) {
        automationControl->onFrameCompleted(app,
                                            renderServices.getRender(),
                                            renderRuntime ? renderRuntime->getPostprocessOutputImageShared() : nullptr,
                                            renderRuntime ? renderRuntime->getActiveViewportImageShared() : nullptr,
                                            renderRuntime ? renderRuntime->getPresentationImageShared() : nullptr,
                                            App::_frameIndex);
    }

    if (AppAutomation::isFrameAutomationEnabled(app)) {
        YA_PROFILE_SCOPE("Frame/Automation");
        YA_PERF_SCOPE(perf::sample::frameAutomation(), perf::metric::cpuTimeMs(), perf::domain::render());
        auto* diagnosticsService = renderRuntime ? &renderRuntime->getDiagnosticsService() : nullptr;

        AppAutomation::onFrameCompleted(app,
                                        AppAutomationFrameContext{
                                            .render                     = renderServices.getRender(),
                                            .postprocessImage           = renderRuntime ? renderRuntime->getPostprocessOutputImageShared() : nullptr,
                                            .viewportImage              = renderRuntime ? renderRuntime->getActiveViewportImageShared() : nullptr,
                                            .presentationImage          = renderRuntime ? renderRuntime->getPresentationImageShared() : nullptr,
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
                                            .frameIndex = App::_frameIndex,
                                        });
    }

    return 0;
}

void AppFrameLoop::tickLogic(App& app, float dt)
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

    if (auto* scene = app.getSceneServices().getActiveScene()) {
        YA_PROFILE_SCOPE("Logic/RuntimeCamera");
        const Extent2D viewportExtent = resolveRuntimeViewportExtent(app);
        syncRuntimeCameraAspect(*scene, viewportExtent);
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
    auto        nativeWindow   = vkRender->getNativeWindow();
    std::string title          = std::format("{}({})", app._ci.title, vkRender->_selectedDeviceInfo.deviceName);
    if (nativeWindow) {
        nativeWindow->setTitle(title);
    }
}

void AppFrameLoop::syncViewportState(App& app)
{
    (void)app;
}

Extent2D AppFrameLoop::resolveViewportExtent(const App& app, RenderRuntime* renderRuntime, const Rect2D& viewportRect)
{
    if (renderRuntime) {
        Extent2D extent = renderRuntime->getViewportExtent();
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

Entity* AppFrameLoop::getPrimaryCamera(const App& app)
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

void AppFrameLoop::prepareRenderFrameState(App& app, float dt)
{
    auto* renderRuntime = app.getRenderServices().getRenderRuntime();
    if (!renderRuntime) {
        app._renderState->frameState = {};
        return;
    }

    Rect2D viewportRect = renderRuntime->getViewportRect();

    (void)dt;

    Entity* runtimeCamera = getPrimaryCamera(app);

    const bool bUseRuntimeCamera = app._appState == AppState::Runtime &&
                                   runtimeCamera && runtimeCamera->isValid() &&
                                   runtimeCamera->hasComponent<CameraComponent>();

    AppRenderFrameState frameState{};
    frameState.viewportRect             = viewportRect;
    frameState.viewportFrameBufferScale = renderRuntime->getViewportFrameBufferScale();
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

uint32_t AppFrameLoop::resolveFlightIndex(const App& app)
{
    auto* render = app.getRenderServices().getRender();
    if (!render) {
        return 0;
    }

    return render->getCurrentFrameIndex() % MAX_FLIGHTS_IN_FLIGHT;
}

std::vector<RenderOverlaySprite2D> AppFrameLoop::buildScreenOverlaySprites(const App& app)
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

void AppFrameLoop::tickRender(App& app, float dt)
{
    auto* renderRuntime = app.getRenderServices().getRenderRuntime();
    if (!renderRuntime) {
        return;
    }

    app.prepareModulesForRender(dt);
    {
        YA_PROFILE_SCOPE("Render/PrepareRenderFrameState");
        prepareRenderFrameState(app, dt);
    }

    auto& diagnostics = renderRuntime->getDiagnosticsService();
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

    renderRuntime->getOffscreenTaskService().tick(app.getTaskManager());

    const uint32_t flightIndex = resolveFlightIndex(app);

    auto* scene = app._sceneManager ? app._sceneManager->getActiveScene() : nullptr;
    // The extracted snapshot (draw items / lights / skinning palettes) is only
    // consumed by the world pipeline; 2D canvas mode disables the world scene
    // graph, so extraction would be pure waste. Drop the stale per-flight
    // snapshot instead, mirroring the world output handling in
    // getViewportDisplayImageShared.
    if (renderRuntime->isWorldSceneRenderEnabled()) {
        YA_PERF_SCOPE(perf::sample::renderExtract(), perf::metric::cpuTimeMs(), perf::domain::render());
        YA_PROFILE_SCOPE("RenderFrameExtractor::extract");
        RenderFrameExtractor::extract(
            RenderFrameExtractor::ExtractInput{
                .scene          = scene,
                .view           = app._renderState->frameState.view,
                .projection     = app._renderState->frameState.projection,
                .cameraPos      = app._renderState->frameState.cameraPos,
                .viewportExtent = renderRuntime->getViewportExtent(),
                .viewOwner      = entt::null,
                .frameIndex     = App::_frameIndex,
                .deltaTime      = dt,
                .shadowSettings = &app.getRenderServices().getShadowSettings(),
            },
            app._renderState->frameDataPerFlight[flightIndex]);
    }
    else {
        app._renderState->frameDataPerFlight[flightIndex].clear();
    }

    RenderPipelineFrameContext pipelineFrame{
        .flightIndex              = flightIndex,
        .deltaTime                = dt,
        .view                     = app._renderState->frameState.view,
        .projection               = app._renderState->frameState.projection,
        .cameraPos                = app._renderState->frameState.cameraPos,
        .viewportRect             = app._renderState->frameState.viewportRect,
        .viewportFrameBufferScale = app._renderState->frameState.viewportFrameBufferScale,
        .frameData                = &app._renderState->frameDataPerFlight[flightIndex],
        .shadowSettings           = &app.getRenderServices().getShadowSettings(),
    };

    auto screenOverlaySprites = AppFrameLoop::buildScreenOverlaySprites(app);

    // Game UI: build the immutable frame snapshot BEFORE the RenderGraph.
    // Command recording consumes only this packet; the live WidgetTree is
    // never touched while recording. Runtime/simulation only (standalone game
    // and PIE); the editor's 3D authoring viewport has no game UI.
    UIFrameSnapshot          uiFrameSnapshot;
    const UIFrameSnapshot*   pUiFrameSnapshot = nullptr;
    if ((app.isRuntimeMode() || app.isSimulationMode()) && scene) {
        if (auto* gameUIHost = app.getGameUIHost()) {
            gameUIHost->setPresentation(pipelineFrame.viewportRect,
                                        glm::vec2(pipelineFrame.viewportFrameBufferScale));
            uiFrameSnapshot  = gameUIHost->buildSnapshot();
            pUiFrameSnapshot = &uiFrameSnapshot;
        }
    }

    renderRuntime->renderFrame(RenderRuntime::FrameInput{
        .overlay = {
            .screenSprites = &screenOverlaySprites,
        },
        // Designated initializers must follow declaration order:
        // presentationExtensions is declared before viewportCompose in FrameInput.
        .presentationExtensions = {
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
                bool bAppended = AppAutomation::appendPresentationCapture(app.getFrameIndex(),
                                                                          graph,
                                                                          presentationOutput,
                                                                          presentationExtent);
                if (auto* automationControl = app.getAutomationControlService()) {
                    bAppended = automationControl->appendPresentationCapture(app.getFrameIndex(),
                                                                             graph,
                                                                             presentationOutput,
                                                                             presentationExtent) ||
                                bAppended;
                }
                return bAppended;
            },
        },
        .viewportCompose = {
            .recordCompose = [&app, dt](ICommandBuffer* commandBuffer)
            {
                if (commandBuffer) {
                    app.recordModuleViewportCompose(*commandBuffer, dt);
                } },
        },
        .pipeline = pipelineFrame,
        .uiFrameSnapshot = pUiFrameSnapshot,
    });
}

} // namespace ya
