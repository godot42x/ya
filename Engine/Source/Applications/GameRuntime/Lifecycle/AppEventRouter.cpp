#include "GameRuntime/App.h"
#include "GUI/Host/GUIWindowChrome.h"
#include "GUI/Host/NativeWindowManager.h"
#include "RHI/NativeWindow.h"

#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"

namespace ya
{
int App::onEvent(const Event& event)
{
    YA_PROFILE_FUNCTION()
    YA_PERF_SCOPE(perf::sample::appEventRoute(), perf::metric::cpuTimeMs(), perf::domain::game());

    const auto* nativeWindowManager = getNativeWindowManager();
    const uint32_t mainWindowID = nativeWindowManager ? nativeWindowManager->getMainWindowID() : 0;

    auto isMainWindowEvent = [&](const WindowEvent& windowEvent)
    {
        return mainWindowID == 0 || windowEvent.getWindowID() == mainWindowID;
    };

    EEvent::T ty       = event.getEventType();
    switch (ty) {
    case EEvent::MouseMoved:
        handleMouseMoved(static_cast<const MouseMoveEvent&>(event));
        break;
    case EEvent::WindowResize:
        if (isMainWindowEvent(static_cast<const WindowResizeEvent&>(event))) {
            handleWindowResized(static_cast<const WindowResizeEvent&>(event));
        }
        break;
    case EEvent::None:
        break;
    case EEvent::WindowClose:
        if (isMainWindowEvent(static_cast<const WindowCloseEvent&>(event))) {
            requestQuit();
        }
        break;
    case EEvent::WindowRestore:
        if (isMainWindowEvent(static_cast<const WindowRestoreEvent&>(event))) {
            _bMinimized = false;
        }
        break;
    case EEvent::WindowMinimize:
        if (isMainWindowEvent(static_cast<const WindowMinimizeEvent&>(event))) {
            _bMinimized = true;
        }
        break;
    case EEvent::WindowFocus:
    case EEvent::WindowFocusLost:
    {
        const auto& windowEvent = static_cast<const WindowEvent&>(event);
        if (isMainWindowEvent(windowEvent) && ty == EEvent::WindowFocusLost) {
            inputRouter.cancelInput(EInputCancelReason::WindowFocusLost);
        }
    } break;
    case EEvent::WindowMoved:
        if (isMainWindowEvent(static_cast<const WindowEvent&>(event))) {
            if (NativeWindowManager* nwm = getNativeWindowManager()) {
                if (INativeWindow* window = nwm->getMainWindow()) {
                    applyWindowChrome(*window, defaultWindowChromeMode(), true);
                }
            }
        }
        break;
    case EEvent::WindowMouseEnter:
        break;
    case EEvent::WindowMouseLeave:
    {
        const auto& windowEvent = static_cast<const WindowEvent&>(event);
        if (isMainWindowEvent(windowEvent)) {
            // The pointer left the window: if it left with the button already
            // physically up, this window will never receive the release and the
            // session it opened is stale. The cancel reason lets the input node
            // compare its cached press against the platform's real state.
            inputRouter.cancelInput(EInputCancelReason::PointerLeftWindow);
        }
    } break;
    case EEvent::AppTick:
    case EEvent::AppUpdate:
    case EEvent::AppRender:
        break;
    case EEvent::AppQuit:
        requestQuit();
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
        (void)inputRouter.routeEvent(event);
        return 0;
    }

    if (dispatchModuleEvent(event)) {
        return 0;
    }

    return 0;
}

bool App::handleWindowResized(const WindowResizeEvent& event)
{
    const auto* nativeWindowManager = getNativeWindowManager();
    if (nativeWindowManager && nativeWindowManager->getMainWindowID() != 0 && event.getWindowID() != nativeWindowManager->getMainWindowID()) {
        return false;
    }

    auto  w           = event.GetWidth();
    auto  h           = event.GetHeight();
    float aspectRatio = h > 0 ? static_cast<float>(w) / static_cast<float>(h) : 1.f;
    YA_CORE_DEBUG("Window({}) resized to {}x{}, aspectRatio: {} ",event.getWindowID(), w, h, aspectRatio);
    if (NativeWindowManager* nwm = getNativeWindowManager()) {
        if (INativeWindow* window = nwm->getMainWindow()) {
            applyWindowChrome(*window, defaultWindowChromeMode(), true);
        }
    }
    return false;
}

void App::handleMouseMoved(const MouseMoveEvent& event)
{
    const auto* nativeWindowManager = getNativeWindowManager();
    const uint32_t mainWindowID = nativeWindowManager ? nativeWindowManager->getMainWindowID() : 0;
    if (mainWindowID != 0 && event.getWindowID() != 0 && event.getWindowID() != mainWindowID) {
        return;
    }
    _lastMousePos = glm::vec2(event.getX(), event.getY());
}

} // namespace ya
