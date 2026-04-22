#include "Runtime/App/Lifecycle/AppFrameLoop.h"

#include "Runtime/App/App.h"
#include "Runtime/App/Utility/FPSCtrl.h"

#include "Core/Manager/Facade.h"
#include "Core/System/FileWatcher.h"

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
    SDL_Event evt;
    SDL_PollEvent(&evt);
    processEvent(app, evt);

    dt += FPSControl::get()->update(dt);

    if (app._bMinimized) {
        SDL_Delay(100);
        return 0;
    }
    if (!app._bPause) {
        tickLogic(app, dt);
    }
    tickRender(app, dt);
    ++App::_frameIndex;
    return 0;
}

int AppFrameLoop::processEvent(App& app, SDL_Event& event)
{
    processSDLEvent(
        event,
        [&app](const auto& e)
        { app.dispatchEvent(e); });
    return 0;
}

void AppFrameLoop::tickLogic(App& app, float dt)
{
    YA_PROFILE_FUNCTION()
    app.taskManager.update();
    Facade.timerManager.onUpdate(dt);
    syncViewportState(app);

    for (auto& sys : app._systems) {
        sys->onUpdate(dt);
    }

    Render2D::onUpdate(dt);

    switch (app._appState) {
    case AppState::Editor:
        break;
    case AppState::Simulation:
    case AppState::Runtime:
    {
        app._luaScriptingSystem->onUpdate(dt);
    } break;
    }

    if (auto* watcher = FileWatcher::get()) {
        watcher->poll();
    }

    app._editorLayer->onUpdate(dt);
    app.inputManager.postUpdate();

    app.inputManager.preUpdate();
    app.cameraController.update(app.camera, app.inputManager, dt);

    auto* render = app.getRender();
    if (!render) {
        return;
    }
    auto        vkRender       = render->as<VulkanRender>();
    auto        windowProvider = vkRender->_windowProvider;
    std::string title          = std::format("{}({})", app._ci.title, vkRender->_selectedDeviceInfo.deviceName);
    SDL_SetWindowTitle(windowProvider->getNativeWindowPtr<SDL_Window>(), title.c_str());

    prepareRenderFrameState(app, dt);
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

void AppFrameLoop::tickRender(App& app, float dt)
{
    auto* renderRuntime = app.getRenderRuntime();
    if (!renderRuntime) {
        return;
    }

    auto* scene = app._sceneManager ? app._sceneManager->getActiveScene() : nullptr;
    RenderFrameExtractor::extract(
        RenderFrameExtractor::ExtractInput{
            .scene          = scene,
            .view           = app._renderFrameState.view,
            .projection     = app._renderFrameState.projection,
            .cameraPos      = app._renderFrameState.cameraPos,
            .viewportExtent = renderRuntime->getViewportExtent(),
            .frameIndex     = App::_frameIndex,
            .deltaTime      = dt,
        },
        app._renderFrameData);

    renderRuntime->renderFrame(RenderRuntime::FrameInput{
        .dt                       = dt,
        .sceneManager             = app._sceneManager,
        .editorLayer              = app._editorLayer,
        .viewportRect             = app._renderFrameState.viewportRect,
        .viewportFrameBufferScale = app._renderFrameState.viewportFrameBufferScale,
        .appMode                  = app._appMode,
        .clicked                  = &app.clicked,
        .view                     = app._renderFrameState.view,
        .projection               = app._renderFrameState.projection,
        .cameraPos                = app._renderFrameState.cameraPos,
        .frameData                = &app._renderFrameData,
    });
}

} // namespace ya
