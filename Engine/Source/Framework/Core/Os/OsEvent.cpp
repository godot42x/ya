#include "Core/Os/OsEvent.h"

#include "Core/Base.h"

#include <vector>

#if USE_SDL
    #include <SDL3/SDL.h>
#endif

namespace ya
{

#if USE_SDL

namespace
{

/// SDL reports buttons as `1 << (button - 1)`; the engine's pointer session
/// indexes them as `1 << EMouse::T` so tree state and platform state compare
/// directly.
[[nodiscard]] uint32_t mouseButtonMask(uint32_t sdlMask)
{
    uint32_t mask = 0;
    for (uint8_t button = EMouse::Left; button <= EMouse::X2; ++button) {
        if ((sdlMask & (1u << (button - 1))) != 0) {
            mask |= 1u << button;
        }
    }
    return mask;
}

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
        press._windowID   = event.button.windowID;
        press._clickCount = event.button.clicks > 0 ? static_cast<uint32_t>(event.button.clicks) : 1u;
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
    query.bValid     = true;
    query.windowID   = SDL_GetWindowID(focusedWindow);
    query.buttonMask = mouseButtonMask(SDL_GetMouseState(&query.x, &query.y));
#endif
    return query;
}

FOsMouseQuery OsEventPump::queryGlobalMouse()
{
    FOsMouseQuery query;
#if USE_SDL
    query.buttonMask = mouseButtonMask(SDL_GetGlobalMouseState(&query.x, &query.y));
    query.bValid     = true;
#endif
    return query;
}

void OsEventPump::warpGlobalMouse(float x, float y)
{
#if USE_SDL
    SDL_WarpMouseGlobal(x, y);
#else
    (void)x;
    (void)y;
#endif
}

namespace
{

struct PendingKey
{
    EKey::T  key            = EKey::NONE;
    bool     bDown          = false;
    uint32_t pollsUntilEmit = 0;
};

std::vector<PendingKey> g_injectedKeys;

} // namespace

void OsEventPump::emitKey(const std::function<void(const Event&)>& emit, EKey::T key, bool bDown)
{
    if (bDown) {
        KeyPressedEvent event;
        event._keyCode = key;
        emit(event);
        return;
    }
    KeyReleasedEvent event;
    event._keyCode = key;
    emit(event);
}

void OsEventPump::enqueueKey(EKey::T key, bool bDown, uint32_t pollsUntilEmit)
{
    g_injectedKeys.push_back(PendingKey{
        .key            = key,
        .bDown          = bDown,
        .pollsUntilEmit = pollsUntilEmit,
    });
}

void OsEventPump::drainInjectedKeys(const std::function<void(const Event&)>& emit)
{
    std::vector<PendingKey> due;
    std::vector<PendingKey> waiting;
    due.reserve(g_injectedKeys.size());
    for (PendingKey& item : g_injectedKeys) {
        if (item.pollsUntilEmit == 0) {
            due.push_back(item);
        }
        else {
            --item.pollsUntilEmit;
            waiting.push_back(item);
        }
    }
    // Events emitted below may enqueue more; those wait for a later drain.
    g_injectedKeys = std::move(waiting);
    for (const PendingKey& item : due) {
        emitKey(emit, item.key, item.bDown);
    }
}

uint32_t guiEventWindowId(const Event& event)
{
    switch (event.getEventType()) {
    case EEvent::WindowClose:
    case EEvent::WindowResize:
    case EEvent::WindowRestore:
    case EEvent::WindowMinimize:
    case EEvent::WindowFocus:
    case EEvent::WindowFocusLost:
    case EEvent::WindowMoved:
    case EEvent::WindowMouseEnter:
    case EEvent::WindowMouseLeave:
        return static_cast<const WindowEvent&>(event).getWindowID();
    case EEvent::MouseMoved:
        return static_cast<const MouseMoveEvent&>(event).getWindowID();
    case EEvent::MouseScrolled:
        return static_cast<const MouseScrolledEvent&>(event).getWindowID();
    case EEvent::MouseButtonPressed:
    case EEvent::MouseButtonReleased:
        return static_cast<const MouseButtonEvent&>(event).getWindowID();
    case EEvent::KeyPressed:
    case EEvent::KeyReleased:
    case EEvent::KeyTyped:
        return static_cast<const KeyEvent&>(event).getWindowID();
    default:
        return 0;
    }
}

} // namespace ya
