#include "Runtime/App/Lifecycle/AppEventRouter.h"

#include "Runtime/App/App.h"

#include "ImGuiHelper.h"

#include "Core/UI/UIManager.h"

namespace ya
{

int App::onEvent(const Event& event)
{
    return AppEventRouter::onEvent(*this, event);
}

int AppEventRouter::onEvent(App& app, const Event& event)
{
    EventProcessState ret = ImGuiManager::get().processEvent(event);
    if (ret != EventProcessState::Continue) {
        return 0;
    }

    bool      bHandled = false;
    EEvent::T ty       = event.getEventType();
    switch (ty) {
    case EEvent::MouseMoved:
    {
        bHandled |= onMouseMoved(app, static_cast<const MouseMoveEvent&>(event));
    } break;
    case EEvent::MouseButtonReleased:
    {
        bHandled |= onMouseButtonReleased(app, static_cast<const MouseButtonReleasedEvent&>(event));
    } break;
    case EEvent::WindowResize:
    {
        bHandled |= onWindowResized(app, static_cast<const WindowResizeEvent&>(event));
    } break;
    case EEvent::KeyReleased:
    {
        bHandled |= onKeyReleased(app, static_cast<const KeyReleasedEvent&>(event));
    } break;
    case EEvent::MouseScrolled:
    {
        bHandled |= onMouseScrolled(app, static_cast<const MouseScrolledEvent&>(event));
    } break;
    case EEvent::None:
        break;
    case EEvent::WindowClose:
        app.requestQuit();
        break;
    case EEvent::WindowRestore:
        app._bMinimized = false;
        break;
    case EEvent::WindowMinimize:
        app._bMinimized = true;
        break;
    case EEvent::WindowFocus:
    case EEvent::WindowFocusLost:
    case EEvent::WindowMoved:
    case EEvent::AppTick:
    case EEvent::AppUpdate:
    case EEvent::AppRender:
        break;
    case EEvent::AppQuit:
        app.requestQuit();
        break;
    case EEvent::KeyPressed:
    case EEvent::KeyTyped:
    case EEvent::MouseButtonPressed:
    case EEvent::EventTypeCount:
    case EEvent::ENUM_MAX:
        break;
    }

    if (bHandled) {
        return 0;
    }

    app.inputManager.processEvent(event);

    Rect2D viewportRect = app._renderRuntime ? app._renderRuntime->getViewportRect() : Rect2D{};
    bool   bInViewport  = FUIHelper::isPointInRect(app._lastMousePos, viewportRect.pos, viewportRect.extent);
    if (bInViewport) {
        UIAppCtx ctx{
            .lastMousePos = app._lastMousePos,
            .bInViewport  = bInViewport,
            .viewportRect = viewportRect,
        };
        app._editorLayer->screenToViewport(app._lastMousePos, ctx.lastMousePos);
        UIManager::get()->onEvent(event, ctx);
    }

    app._editorLayer->onEvent(event);
    return 0;
}

bool AppEventRouter::onWindowResized(App& app, const WindowResizeEvent& event)
{
    auto  w           = event.GetWidth();
    auto  h           = event.GetHeight();
    float aspectRatio = h > 0 ? static_cast<float>(w) / static_cast<float>(h) : 1.f;
    YA_CORE_DEBUG("Window resized to {}x{}, aspectRatio: {} ", w, h, aspectRatio);
    app._windowSize = {w, h};
    return false;
}

bool AppEventRouter::onKeyReleased(App& app, const KeyReleasedEvent& event)
{
    if (event.getKeyCode() == EKey::Escape) {
        YA_CORE_INFO("{}", event.toString());
        app.requestQuit();
        return true;
    }
    return false;
}

bool AppEventRouter::onMouseMoved(App& app, const MouseMoveEvent& event)
{
    app._lastMousePos = glm::vec2(event.getX(), event.getY());
    return false;
}

bool AppEventRouter::onMouseButtonReleased(App& app, const MouseButtonReleasedEvent& event)
{
    switch (app._appMode) {
    case Control:
        break;
    case Drawing:
    {
        if (event.GetMouseButton() == EMouse::Left) {
            app.clicked.push_back(app._lastMousePos);
        }
    } break;
    }

    return false;
}

bool AppEventRouter::onMouseScrolled(App& app, const MouseScrolledEvent& event)
{
    (void)app;
    (void)event;
    return false;
}

} // namespace ya
