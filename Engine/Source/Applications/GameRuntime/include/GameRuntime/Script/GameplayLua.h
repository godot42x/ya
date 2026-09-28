#pragma once

#include "Core/Api.h"

namespace ya
{

struct GameUIHost;
struct LuaScriptingSystem;

/// Gameplay Lua surface for the 2D sample: sprite read/write, sprite spawn,
/// and the mounted-document text/visibility bridge. Registered by the host
/// after `LuaScriptingSystem::init`. GUI widgets stay free of Lua; this file
/// lives in GameRuntime because that is the module that already sees both
/// the script state and `Sprite2DComponent`.
YA_GAME_RUNTIME_API void bindGameplayLua(LuaScriptingSystem& scripting, GameUIHost& ui);

} // namespace ya
