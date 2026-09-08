#include "Core/Os/OsEvent.h"

#include "Core/Base.h"

#if USE_SDL
    #include <SDL3/SDL.h>
#endif

namespace ya
{

#if USE_SDL

namespace
{

void emitSdlEvent(const SDL_Event& event, const std::function<void(const Event&)>& emit)
{
    switch (event.type) {
    case SDL_EVENT_QUIT: {
        emit(AppQuitEvent());
        break;
    }
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED: {
        emit(WindowCloseEvent(event.window.windowID));
        break;
    }
    case SDL_EVENT_WINDOW_RESIZED: {
        emit(WindowResizeEvent(event.window.windowID, event.window.data1, event.window.data2));
        break;
    }
    case SDL_EVENT_WINDOW_MOVED: {
        emit(WindowMovedEvent(event.window.windowID,
                              static_cast<uint32_t>(event.window.data1),
                              static_cast<uint32_t>(event.window.data2)));
        break;
    }
    case SDL_EVENT_WINDOW_MINIMIZED: {
        emit(WindowMinimizeEvent(event.window.windowID));
        break;
    }
    case SDL_EVENT_WINDOW_MAXIMIZED:
    case SDL_EVENT_WINDOW_RESTORED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_METAL_VIEW_RESIZED: {
        emit(WindowRestoreEvent(event.window.windowID));
        break;
    }
    case SDL_EVENT_WINDOW_FOCUS_GAINED: {
        emit(WindowFocusEvent(event.window.windowID));
        break;
    }
    case SDL_EVENT_WINDOW_FOCUS_LOST: {
        emit(WindowFocusLostEvent(event.window.windowID));
        break;
    }
    case SDL_EVENT_WINDOW_MOUSE_ENTER: {
        emit(WindowMouseEnterEvent(event.window.windowID));
        break;
    }
    case SDL_EVENT_WINDOW_MOUSE_LEAVE: {
        emit(WindowMouseLeaveEvent(event.window.windowID));
        break;
    }
    case SDL_EVENT_KEY_DOWN: {
        KeyPressedEvent ev;
        ev._keyCode  = EKey::fromNativeKeycode(static_cast<uint32_t>(event.key.key));
        ev._mod      = static_cast<uint32_t>(event.key.mod);
        ev.bRepeat   = event.key.repeat;
        ev._windowID = event.key.windowID;
        emit(ev);
        break;
    }
    case SDL_EVENT_KEY_UP: {
        KeyReleasedEvent ev;
        ev._keyCode  = EKey::fromNativeKeycode(static_cast<uint32_t>(event.key.key));
        ev._mod      = static_cast<uint32_t>(event.key.mod);
        ev._windowID = event.key.windowID;
        emit(ev);
        break;
    }
    case SDL_EVENT_TEXT_INPUT: {
        KeyTypedEvent typed(event.text.text ? event.text.text : "");
        typed._windowID = event.text.windowID;
        emit(typed);
        break;
    }
    case SDL_EVENT_MOUSE_MOTION: {
        MouseMoveEvent move(event.motion.x, event.motion.y, event.motion.xrel, event.motion.yrel);
        move._windowID = event.motion.windowID;
        emit(move);
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        MouseMoveEvent move(event.button.x, event.button.y);
        move._windowID = event.button.windowID;
        emit(move);
        MouseButtonPressedEvent press(EMouse::fromNativeMouseButton(event.button.button));
        press._windowID = event.button.windowID;
        emit(press);
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        MouseMoveEvent move(event.button.x, event.button.y);
        move._windowID = event.button.windowID;
        emit(move);
        MouseButtonReleasedEvent release(EMouse::fromNativeMouseButton(event.button.button));
        release._windowID = event.button.windowID;
        emit(release);
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        MouseMoveEvent move(event.wheel.mouse_x, event.wheel.mouse_y);
        move._windowID = event.wheel.windowID;
        emit(move);
        MouseScrolledEvent scroll(event.wheel.x, event.wheel.y);
        scroll._windowID = event.wheel.windowID;
        emit(scroll);
        break;
    }
    default: {
        break;
    }
    }
}

} // namespace

#endif

void OsEventPump::pump()
{
#if USE_SDL
    SDL_PumpEvents();
#endif
}

void OsEventPump::poll(const std::function<void(const Event&)>& emit)
{
#if USE_SDL
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        emitSdlEvent(event, emit);
    }
#else
    (void)emit;
#endif
}

FOsMouseQuery OsEventPump::queryMouse()
{
    FOsMouseQuery query;
#if USE_SDL
    SDL_Window* focusedWindow = SDL_GetMouseFocus();
    if (!focusedWindow) {
        return query;
    }
    query.bHasWindow = true;
    query.windowID   = SDL_GetWindowID(focusedWindow);
    SDL_GetMouseState(&query.x, &query.y);
#endif
    return query;
}

} // namespace ya
