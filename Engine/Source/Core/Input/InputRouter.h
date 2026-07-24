#pragma once

#include "Core/Event.h"
#include "Core/Input/InputMode.h"
#include "Core/KeyCode.h"

#include <SDL3/SDL.h>

namespace ya
{

class InputRouter
{
public:
    InputRouter() = default;

    // --- Window binding (set once during init) ---
    void setWindow(SDL_Window* window) { _window = window; }
    [[nodiscard]] SDL_Window* getWindow() const { return _window; }

    // --- Mode management ---
    void setInputMode(EInputMode mode);
    [[nodiscard]] EInputMode getInputMode() const { return _inputMode; }

    // --- Mouse capture ---
    void captureMouse();
    void releaseMouse();
    [[nodiscard]] bool isMouseCaptured() const { return _bMouseCaptured; }
    void setMouseCaptureMode(EMouseCapture mode) { _mouseCaptureMode = mode; }
    [[nodiscard]] EMouseCapture getMouseCaptureMode() const { return _mouseCaptureMode; }

    // --- Viewport state (set by viewport UI each frame) ---
    void setViewportFocused(bool focused) { _bViewportFocused = focused; }
    void setViewportHovered(bool hovered) { _bViewportHovered = hovered; }
    void setViewportCenter(float x, float y) { _viewportCenterX = x; _viewportCenterY = y; }
    [[nodiscard]] bool isViewportFocused() const { return _bViewportFocused; }
    [[nodiscard]] bool isViewportHovered() const { return _bViewportHovered; }

    // Convenience: called from EditorAppExtension::onEvent.
    // Returns true if the event was consumed (should NOT reach InputManager).
    [[nodiscard]] bool routeEvent(const Event& event, bool bImGuiHandled);

private:
    SDL_Window*     _window              = nullptr;
    EInputMode      _inputMode           = EInputMode::Editor;
    EMouseCapture   _mouseCaptureMode    = EMouseCapture::None;
    bool            _bMouseCaptured      = false;
    bool            _bViewportFocused    = false;
    bool            _bViewportHovered    = false;
    float           _viewportCenterX     = 0.0f;
    float           _viewportCenterY     = 0.0f;
};

} // namespace ya
