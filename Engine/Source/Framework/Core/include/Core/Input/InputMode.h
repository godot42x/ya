#pragma once

#include <cstdint>

namespace ya
{

/// Runtime input mode: who receives input and the cursor baseline. Owned by
/// App (runtime layer); game code toggles it via App::setInputMode /
/// pushInputMode / popInputMode (script API: input.*).
enum class EInputMode : uint8_t
{
    GameAndUI, // (default) game-UI picking first; a Stop hit consumes
               // exclusively, misses / Pass hits fall through to the game.
               // Cursor visible.
    GameOnly,  // game-UI picking disabled; everything goes to the game.
               // Cursor hidden.
    UIOnly,    // game input disabled; UI receives everything. Cursor visible.
};

} // namespace ya
