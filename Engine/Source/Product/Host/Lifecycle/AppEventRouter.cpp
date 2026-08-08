#include "Product/Host/Lifecycle/AppEventRouter.h"

#include "Product/Host/App.h"

#include "Foundation/Core/Profiling/PerfKeys.h"
#include "Foundation/Core/Profiling/PerfState.h"

namespace ya
{
namespace
{

} // namespace

int App::onEvent(const Event& event)
{
    return AppEventRouter::onEvent(*this, event);
}

int AppEventRouter::onEvent(App& app, const Event& event)
{
    YA_PROFILE_FUNCTION()
    YA_PERF_SCOPE(perf::sample::appEventRoute(), perf::metric::cpuTimeMs(), perf::domain::game());

    const auto* windowManager = app.getWindowManager();
    const uint32_t mainWindowID = windowManager ? windowManager->getMainWindowID() : 0;

    auto isMainWindowEvent = [&](const WindowEvent& windowEvent)
    {
        return mainWindowID == 0 || windowEvent.getWindowID() == mainWindowID;
    };

    EEvent::T ty       = event.getEventType();
    switch (ty) {
    case EEvent::MouseMoved:
        onMouseMoved(app, static_cast<const MouseMoveEvent&>(event));
        break;
    case EEvent::WindowResize:
        if (isMainWindowEvent(static_cast<const WindowResizeEvent&>(event))) {
            onWindowResized(app, static_cast<const WindowResizeEvent&>(event));
        }
        break;
    case EEvent::None:
        break;
    case EEvent::WindowClose:
        if (isMainWindowEvent(static_cast<const WindowCloseEvent&>(event))) {
            app.requestQuit();
        }
        break;
    case EEvent::WindowRestore:
        if (isMainWindowEvent(static_cast<const WindowRestoreEvent&>(event))) {
            app._bMinimized = false;
        }
        break;
    case EEvent::WindowMinimize:
        if (isMainWindowEvent(static_cast<const WindowMinimizeEvent&>(event))) {
            app._bMinimized = true;
        }
        break;
    case EEvent::WindowFocus:
    case EEvent::WindowFocusLost:
    {
        const auto& windowEvent = static_cast<const WindowEvent&>(event);
        if (isMainWindowEvent(windowEvent) && ty == EEvent::WindowFocusLost) {
            app.inputRouter.cancelInput(EInputCancelReason::WindowFocusLost);
        }
    } break;
    case EEvent::WindowMoved:
    case EEvent::AppTick:
    case EEvent::AppUpdate:
    case EEvent::AppRender:
        break;
    case EEvent::AppQuit:
        app.requestQuit();
        break;
    case EEvent::KeyPressed:
    case EEvent::KeyReleased:
    case EEvent::KeyTyped:
    case EEvent::MouseScrolled:
    case EEvent::MouseButtonPressed:
    case EEvent::MouseButtonReleased:
    case EEvent::EventTypeCount:
    case EEvent::ENUM_MAX:
        break;
    }

    if (event.isInCategory(EEventCategory::Input)) {
        YA_PROFILE_SCOPE("App/InputEvent");
        YA_PERF_SCOPE(perf::sample::appInputEvent(), perf::metric::cpuTimeMs(), perf::domain::game());
        app.inputRouter.routeEvent(event);
        return 0;
    }

    if (app.dispatchHostModuleEvent(event)) {
        return 0;
    }

    return 0;
}

bool AppEventRouter::onWindowResized(App& app, const WindowResizeEvent& event)
{
    const auto* windowManager = app.getWindowManager();
    if (windowManager && windowManager->getMainWindowID() != 0 && event.getWindowID() != windowManager->getMainWindowID()) {
        return false;
    }

    auto  w           = event.GetWidth();
    auto  h           = event.GetHeight();
    float aspectRatio = h > 0 ? static_cast<float>(w) / static_cast<float>(h) : 1.f;
    YA_CORE_DEBUG("Window({}) resized to {}x{}, aspectRatio: {} ",event.getWindowID(), w, h, aspectRatio);
    app._windowSize = {w, h};
    return false;
}

bool AppEventRouter::onMouseMoved(App& app, const MouseMoveEvent& event)
{
    app._lastMousePos = glm::vec2(event.getX(), event.getY());
    return false;
}

} // namespace ya
