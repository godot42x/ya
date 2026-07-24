#include "Core/Input/InputRouter.h"
#include "Core/Log.h"

namespace ya
{

// --- Mode management ---

void InputRouter::setInputMode(EInputMode mode)
{
    if (_inputMode != mode) {
        YA_CORE_DEBUG("InputRouter: mode {} -> {}", static_cast<int>(_inputMode), static_cast<int>(mode));
        _inputMode = mode;
    }
}

// --- Mouse capture ---

void InputRouter::captureMouse()
{
    if (_bMouseCaptured) return;
    if (!_window) {
        YA_CORE_ERROR("InputRouter::captureMouse: no SDL_Window set");
        return;
    }

    _bMouseCaptured = true;
    SDL_SetWindowRelativeMouseMode(_window, true);

    // Warp cursor to viewport center so delta starts from a known position
    if (_viewportCenterX > 0 || _viewportCenterY > 0) {
        int wx = 0, wy = 0;
        SDL_GetWindowPosition(_window, &wx, &wy);
        SDL_WarpMouseInWindow(_window,
                              static_cast<float>(wx) + _viewportCenterX,
                              static_cast<float>(wy) + _viewportCenterY);
    }

    YA_CORE_DEBUG("InputRouter: mouse captured (relative mode ON)");
}

void InputRouter::releaseMouse()
{
    if (!_bMouseCaptured) return;
    if (!_window) {
        _bMouseCaptured = false;
        return;
    }

    SDL_SetWindowRelativeMouseMode(_window, false);
    _bMouseCaptured = false;
    YA_CORE_DEBUG("InputRouter: mouse released (relative mode OFF)");
}

bool InputRouter::routeEvent(const Event& event, bool bImGuiHandled)
{
    const bool bKeyboard = event.isInCategory(EEventCategory::Keyboard);
    const bool bMouse    = event.isInCategory(EEventCategory::Mouse);

    switch (_inputMode) {
    case EInputMode::Editor:
        // Editor mode: ImGui/editor tools consume events.
        // Game systems (Lua) do NOT receive input.
        return bImGuiHandled;

    case EInputMode::Game:
        // Game mode: determine passthrough based on capture + viewport state
        if (_bMouseCaptured) {
            // Mouse is captured: ALL input goes to game, never to editor
            return false;
        }
        if (bKeyboard) {
            // Keyboard: passthrough if viewport is focused
            // (viewport retains keyboard focus even when mouse is outside)
            return _bViewportFocused ? false : bImGuiHandled;
        }
        if (bMouse) {
            // Mouse: passthrough only when viewport focused or hovered
            return (_bViewportFocused || _bViewportHovered) ? false : bImGuiHandled;
        }
        return bImGuiHandled;

    case EInputMode::GameAndUI:
        // Game + UI overlay: similar to Game but ImGui can also capture
        if (_bMouseCaptured) {
            // Mouse captured: input goes to game; ImGui overlay still gets a look
            // but only when it explicitly requests focus (e.g. pause menu)
            return false;
        }
        if (bKeyboard) {
            // If ImGui handled it (e.g. text input for console), respect that
            if (bImGuiHandled) return true;
            return _bViewportFocused ? false : bImGuiHandled;
        }
        if (bMouse) {
            if (bImGuiHandled) return true;
            return (_bViewportFocused || _bViewportHovered) ? false : bImGuiHandled;
        }
        return bImGuiHandled;
    }

    return bImGuiHandled;
}

} // namespace ya
