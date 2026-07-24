#pragma once

#include <cstdint>

namespace ya
{

enum class EInputMode : uint8_t
{
    Editor,     // Editor tools receive input (Stopped state)
    Game,       // Game receives all input, editor tools do not respond
    GameAndUI,  // Game receives input, but editor UI overlays can also receive (e.g. pause menu)
};

enum class EMouseCapture : uint8_t
{
    None,            // Cursor visible, free to move in/out of viewport
    Captured,        // Cursor hidden + relative mode, all mouse events go to game
    CaptureOnClick,  // Wait for viewport click, then auto-capture
};

} // namespace ya
