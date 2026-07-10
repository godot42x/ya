#include "Runtime/App/Lifecycle/AppFrameLoop.h"

#include "Runtime/App/App.h"
#include "Runtime/App/Lifecycle/AppAutomation.h"
#include "Runtime/App/Utility/FPSCtrl.h"

#include "Core/Async/TaskQueue.h"
#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Core/Manager/Facade.h"
#include "Core/System/FileWatcher.h"

#include "ECS/Component/2D/BillboardComponent.h"
#include "ECS/Component/CameraComponent.h"
#include "ECS/Component/PlayerComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/System/LuaScriptingSystem.h"

#include "Platform/Render/Vulkan//VulkanRender.h"

#include "Render/2D/Render2D.h"

#include "Runtime/App/Utility/RenderFrameExtractor.h"
#include "Runtime/App/Utility/SDLMisc.h"

#include "Scene/Scene.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <format>

namespace ya
{

int App::run()
{
    return AppFrameLoop::run(*this);
}

int App::iterate(float dt)
{
    return AppFrameLoop::iterate(*this, dt);
}

int App::processEvent(SDL_Event& event)
{
    return AppFrameLoop::processEvent(*this, event);
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

Entity* App::getPrimaryCamera() const
{
    return AppFrameLoop::getPrimaryCamera(*this);
}

void App::prepareRenderFrameState(float dt)
{
    AppFrameLoop::prepareRenderFrameState(*this, dt);
}

void App::tickRender(float dt)
{
    AppFrameLoop::tickRender(*this, dt);
}

int AppFrameLoop::run(App& app)
{
    app._startTime = std::chrono::steady_clock::now();
    app._lastTime  = app._startTime;

    while (app.bRunning) {
        App::time_point_t now        = App::clock_t::now();
        auto              dtMicroSec = std::chrono::duration_cast<std::chrono::microseconds>(now - app._lastTime).count();
        float             dtSec      = (float)((double)dtMicroSec / 1000000.0);
        dtSec                        = std::max(dtSec, 0.0001f);
        app._lastTime                = now;

        if (auto result = iterate(app, dtSec); result != 0) {
            break;
        }
    }

    return 0;
}

int AppFrameLoop::iterate(App& app, float dt)
{
    YA_PROFILE_FUNCTION()
    YA_PERF_FUNCTION(perf::metric::cpuTimeMs(), perf::domain::render())

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

    SDL_Event evt;
    {
        YA_PROFILE_SCOPE("Frame/EventPump");
        YA_PERF_SCOPE(perf::sample::frameEventPump(), perf::metric::cpuTimeMs(), perf::domain::game());
        if (SDL_PollEvent(&evt)) {
            processEvent(app, evt);
        }
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
        YA_PERF_SCOPE(perf::sample::renderFlushCallbacks(), perf::metric::cpuTimeMs(), perf::domain::render());
        TaskQueue::get().processMainThreadCallbacks();
    }
    ++App::_frameIndex;
    if (AppAutomation::isFrameAutomationEnabled(app)) {
        YA_PROFILE_SCOPE("Frame/Automation");
        YA_PERF_SCOPE(perf::sample::frameAutomation(), perf::metric::cpuTimeMs(), perf::domain::render());
        AppAutomation::onFrameCompleted(app);
    }

    return 0;
}

int AppFrameLoop::processEvent(App& app, SDL_Event& event)
{
    YA_PROFILE_FUNCTION()
    processSDLEvent(
        event,
        [&app](const auto& e)
        { app.dispatchEvent(e); });
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
        Facade.timerManager.onUpdate(dt);
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

    {
        YA_PROFILE_SCOPE("Logic/Render2DUpdate");
        Render2D::onUpdate(dt);
    }

    switch (app._appState) {
    case AppState::Editor:
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

    {
        YA_PROFILE_SCOPE("Logic/EditorUpdate");
        app._editorLayer->onUpdate(dt);
    }
    {
        YA_PROFILE_SCOPE("Logic/InputPostUpdate");
        app.inputManager.postUpdate();
    }

    {
        YA_PROFILE_SCOPE("Logic/InputPreUpdate");
        app.inputManager.preUpdate();
    }
    {
        YA_PROFILE_SCOPE("Logic/EditorCamera");
        app.cameraController.update(app.camera, app.inputManager, dt);
    }

    auto* render = app.getRender();
    if (!render) {
        return;
    }
    auto        vkRender       = render->as<VulkanRender>();
    auto        windowProvider = vkRender->_windowProvider;
    std::string title          = std::format("{}({})", app._ci.title, vkRender->_selectedDeviceInfo.deviceName);
    SDL_SetWindowTitle(windowProvider->getNativeWindowPtr<SDL_Window>(), title.c_str());

    {
        YA_PROFILE_SCOPE("Logic/PrepareRenderFrameState");
        prepareRenderFrameState(app, dt);
    }
}

void AppFrameLoop::syncViewportState(App& app)
{
    auto* renderRuntime = app.getRenderRuntime();
    if (!renderRuntime || !app._editorLayer) {
        return;
    }

    Rect2D pendingRect;
    if (!app._editorLayer->getPendingViewportResize(pendingRect)) {
        return;
    }

    renderRuntime->onViewportResized(pendingRect);
    if (pendingRect.extent.x > 0 && pendingRect.extent.y > 0) {
        app.camera.setAspectRatio(pendingRect.extent.x / pendingRect.extent.y);
    }
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

    for (const auto& [entity, cameraComp, playerComp] :
         registry.view<CameraComponent, PlayerComponent>().each()) {
        (void)cameraComp;
        (void)playerComp;
        return scene->getEntityByEnttID(entity);
    }

    for (const auto& [entity, cameraComp] :
         registry.view<CameraComponent>().each()) {
        if (cameraComp.bPrimary) {
            return scene->getEntityByEnttID(entity);
        }
    }

    return nullptr;
}

void AppFrameLoop::prepareRenderFrameState(App& app, float dt)
{
    auto* renderRuntime = app.getRenderRuntime();
    if (!renderRuntime) {
        app._renderFrameState = {};
        return;
    }

    Rect2D viewportRect = renderRuntime->getViewportRect();

    Entity* runtimeCamera = getPrimaryCamera(app);
    if (runtimeCamera && runtimeCamera->isValid()) {
        auto     cc             = runtimeCamera->getComponent<CameraComponent>();
        auto     tc             = runtimeCamera->getComponent<TransformComponent>();
        Extent2D viewportExtent = resolveViewportExtent(app, renderRuntime, viewportRect);
        app.cameraController.update(*tc, *cc, app.inputManager, viewportExtent, dt);
        if (viewportExtent.height > 0) {
            cc->setAspectRatio(static_cast<float>(viewportExtent.width) / static_cast<float>(viewportExtent.height));
        }
    }

    const bool bUseRuntimeCamera = app._appState == AppState::Runtime &&
                                   runtimeCamera && runtimeCamera->isValid() &&
                                   runtimeCamera->hasComponent<CameraComponent>();

    app._renderFrameState.viewportRect             = viewportRect;
    app._renderFrameState.viewportFrameBufferScale = renderRuntime->getViewportFrameBufferScale();
    if (bUseRuntimeCamera) {
        auto cc                        = runtimeCamera->getComponent<CameraComponent>();
        auto tc                        = runtimeCamera->getComponent<TransformComponent>();
        app._renderFrameState.view       = cc->getFreeView();
        app._renderFrameState.projection = cc->getProjection();
        app._renderFrameState.cameraPos  = tc->getWorldPosition();
        return;
    }

    app._renderFrameState.view       = app.camera.getViewMatrix();
    app._renderFrameState.projection = app.camera.getProjectionMatrix();
    app._renderFrameState.cameraPos  = app.camera.getPosition();
}

uint32_t AppFrameLoop::resolveFlightIndex(const App& app)
{
    auto* render = app.getRender();
    if (!render) {
        return 0;
    }

    return render->getCurrentFrameIndex() % MAX_FLIGHTS_IN_FLIGHT;
}

namespace
{

std::vector<RenderOverlaySprite2D> buildScreenOverlaySprites(const App& app)
{
    std::vector<RenderOverlaySprite2D> sprites;
    if (app._appMode != AppMode::Drawing || !app._editorLayer || app.clicked.empty()) {
        return sprites;
    }

    sprites.reserve(app.clicked.size());
    for (size_t idx = 0; idx < app.clicked.size(); ++idx) {
        const auto& screenPos = app.clicked[idx];
        auto textureHandle = idx % 2 == 0
                               ? AssetManager::get()->getTextureByName("uv1")
                               : AssetManager::get()->getTextureByName("face");
        auto* texture = textureHandle.get();
        YA_CORE_ASSERT(texture, "Texture not found");

        glm::vec2 viewportPos;
        if (!app._editorLayer->screenToViewport(screenPos, viewportPos)) {
            continue;
        }

        RenderOverlaySprite2D sprite;
        sprite.viewportPos = viewportPos;
        sprite.size        = {50.0f, 50.0f};
        sprite.texture     = texture;
        sprites.push_back(sprite);
    }

    return sprites;
}

std::vector<RenderOverlaySprite3D> buildWorldOverlaySprites(const App& app, Scene* scene, const RenderPipelineFrameContext& pipelineFrame)
{
    (void)app;
    std::vector<RenderOverlaySprite3D> sprites;
    if (!scene) {
        return sprites;
    }

    const auto view = scene->getRegistry().view<BillboardComponent, TransformComponent>();
    sprites.reserve(view.size_hint());

    const glm::vec2 screenSize(30.0f, 30.0f);
    const float viewportHeight = pipelineFrame.viewportRect.extent.y;
    if (viewportHeight <= 0.0f) {
        return sprites;
    }
    const float scaleFactor = screenSize.x / viewportHeight;

    for (const auto& [entity, billboard, transfCompp] : view.each()) {
        (void)entity;

        auto texture = billboard.image.hasPath() ? billboard.image.getResolvedTexture().get() : nullptr;
        const auto& pos = transfCompp.getWorldPosition();

        glm::vec3 billboardToCamera = pipelineFrame.cameraPos - pos;
        float     distance          = glm::length(billboardToCamera);
        if (distance <= 0.0f) {
            continue;
        }
        billboardToCamera = glm::normalize(billboardToCamera);

        glm::vec3 forward = billboardToCamera;
        glm::vec3 worldUp = glm::vec3(0, 1, 0);
        glm::vec3 right   = glm::normalize(glm::cross(worldUp, forward));
        glm::vec3 up      = glm::cross(forward, right);

        glm::mat4 rot(1.0f);
        rot[0] = glm::vec4(right, 0.0f);
        rot[1] = glm::vec4(up, 0.0f);
        rot[2] = glm::vec4(forward, 0.0f);

        float     factor = scaleFactor * distance * 2.0f;
        glm::vec3 scale  = glm::vec3(factor, factor, 1.0f);

        glm::mat4 trans = glm::mat4(1.0f);
        trans           = glm::translate(trans, pos);
        trans           = trans * rot;
        trans           = glm::scale(trans, scale);

        RenderOverlaySprite3D sprite;
        sprite.worldTransform = trans;
        sprite.texture        = texture;
        sprites.push_back(sprite);
    }

    return sprites;
}

} // namespace

void AppFrameLoop::tickRender(App& app, float dt)
{
    auto* renderRuntime = app.getRenderRuntime();
    if (!renderRuntime) {
        return;
    }

    renderRuntime->beginFrameDiagnostics();

    struct DiagnosticsGuard
    {
        RenderRuntime* runtime = nullptr;

        ~DiagnosticsGuard()
        {
            if (runtime) {
                runtime->endFrameDiagnostics();
            }
        }
    } diagnosticsGuard{.runtime = renderRuntime};

    renderRuntime->tickOffscreenTasks();

    const uint32_t flightIndex = resolveFlightIndex(app);

    auto* scene = app._sceneManager ? app._sceneManager->getActiveScene() : nullptr;
    {
        YA_PERF_SCOPE(perf::sample::renderExtract(), perf::metric::cpuTimeMs(), perf::domain::render());
        RenderFrameExtractor::extract(
            RenderFrameExtractor::ExtractInput{
                .scene          = scene,
                .view           = app._renderFrameState.view,
                .projection     = app._renderFrameState.projection,
                .cameraPos      = app._renderFrameState.cameraPos,
                .viewportExtent = renderRuntime->getViewportExtent(),
                .frameIndex     = App::_frameIndex,
                .deltaTime      = dt,
                .shadowSettings = &app.getShadowSettings(),
            },
            app._renderFrameDataPerFlight[flightIndex]);
    }

    RenderPipelineFrameContext pipelineFrame{
        .flightIndex              = flightIndex,
        .deltaTime                = dt,
        .view                     = app._renderFrameState.view,
        .projection               = app._renderFrameState.projection,
        .cameraPos                = app._renderFrameState.cameraPos,
        .viewportRect             = app._renderFrameState.viewportRect,
        .viewportFrameBufferScale = app._renderFrameState.viewportFrameBufferScale,
        .frameData                = &app._renderFrameDataPerFlight[flightIndex],
        .shadowSettings           = &app.getShadowSettings(),
    };

    auto screenOverlaySprites = buildScreenOverlaySprites(app);
    auto worldOverlaySprites  = buildWorldOverlaySprites(app, scene, pipelineFrame);

    renderRuntime->renderFrame(RenderRuntime::FrameInput{
        .overlay = {
            .screenSprites = &screenOverlaySprites,
            .worldSprites  = &worldOverlaySprites,
        },
        .editor = {
            .target = app._editorLayer,
        },
        .automation = {
            .recordPresentationCapture = [&app](ICommandBuffer* cmdBuf)
            {
                auto* renderRuntime = app.getRenderRuntime();
                if (!renderRuntime) {
                    return;
                }

                AppAutomation::recordPresentationCapture(renderRuntime->getPresentationTexture(),
                                                         app.getFrameIndex(),
                                                         cmdBuf);
            },
        },
        .pipeline = pipelineFrame,
    });
}

} // namespace ya
