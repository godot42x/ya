#include "Runtime/Application/Lifecycle/AppEventRouter.h"

#include "Runtime/Application/App.h"

#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"

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

    EEvent::T ty       = event.getEventType();
    switch (ty) {
    case EEvent::MouseMoved:
        onMouseMoved(app, static_cast<const MouseMoveEvent&>(event));
        break;
    case EEvent::WindowResize:
        onWindowResized(app, static_cast<const WindowResizeEvent&>(event));
        break;
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
    {
        if (ty == EEvent::WindowFocusLost) {
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
