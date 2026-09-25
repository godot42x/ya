#include "App/Kernel/SdlEventSource.h"

#include "Core/Os/OsEvent.h"

namespace ya
{

void SdlEventSource::pollEvents(const std::function<void(const Event&)>& emit)
{
    OsEventPump::pump();
    if (!_bPointerKnown) {
        const FOsMouseQuery mouse = OsEventPump::queryMouse();
        if (mouse.bHasWindow && isHostWindow(mouse.windowID)) {
            MouseMoveEvent move(mouse.x, mouse.y);
            move._windowID = mouse.windowID;
            emit(move);
            _bPointerKnown = true;
        }
    }

    OsEventPump::poll([&](const Event& event) {
        if (!isHostWindow(guiEventWindowId(event))) {
            return;
        }
        switch (event.getEventType()) {
        case EEvent::WindowMouseEnter: {
            const auto& enter = static_cast<const WindowMouseEnterEvent&>(event);
            const FOsMouseQuery mouse = OsEventPump::queryMouse();
            MouseMoveEvent move(mouse.x, mouse.y);
            move._windowID = enter.getWindowID();
            emit(move);
            emit(WindowFocusEvent(enter.getWindowID()));
            _bPointerKnown = true;
            break;
        }
        case EEvent::WindowMouseLeave: {
            const auto& leaveEvent = static_cast<const WindowMouseLeaveEvent&>(event);
            MouseMoveEvent leave(-1000000.0f, -1000000.0f);
            leave._windowID = leaveEvent.getWindowID();
            emit(leave);
            _bPointerKnown = false;
            // The leave itself still has to reach the app: it is the
            // boundary where a pointer session the platform will not
            // release any more has to be reconciled against the physical
            // button state (the far-pointer move above only clears hover).
            emit(leaveEvent);
            break;
        }
        case EEvent::MouseMoved:
        case EEvent::MouseButtonPressed:
        case EEvent::MouseButtonReleased:
        case EEvent::MouseScrolled: {
            emit(event);
            _bPointerKnown = true;
            break;
        }
        default: {
            emit(event);
            break;
        }
        }
    });
}

} // namespace ya
