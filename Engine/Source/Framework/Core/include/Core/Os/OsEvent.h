#pragma once

#include "Core/Api.h"
#include "Core/Event.h"

#include <functional>

namespace ya
{

struct FOsMouseQuery
{
    uint32_t windowID   = 0;
    float    x          = 0.0f;
    float    y          = 0.0f;
    /// Physically held buttons, encoded `1u << EMouse::T` (the encoding
    /// WidgetTree's pointer session uses). Zero means every button is up, which
    /// is how a host proves a cached press the platform already ended is dead.
    uint32_t buttonMask = 0;
    bool     bHasWindow = false;
    bool     bValid     = false;
};

/// Process-wide native event pump. SDL is an implementation detail; hosts
/// receive Core `Event` values and must not include SDL headers.
struct YA_CORE_API OsEventPump
{
    static void pump();
    static void poll(const std::function<void(const Event&)>& emit);
    [[nodiscard]] static FOsMouseQuery queryMouse();
    /// Screen-space pointer, independent of mouse-focus / capture window.
    /// Used by cross-window drag hit-testing while `SDL_CaptureMouse` keeps
    /// events tagged to the source window.
    [[nodiscard]] static FOsMouseQuery queryGlobalMouse();
    static void warpGlobalMouse(float x, float y);
};

} // namespace ya
