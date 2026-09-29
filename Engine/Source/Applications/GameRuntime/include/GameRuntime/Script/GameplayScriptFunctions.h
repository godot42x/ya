#pragma once

#include "Core/Api.h"

namespace ya
{

/// Gameplay module functions every script backend projects (`world.find` in
/// Lua, `ya.world.find` in JS). Registered through the script export
/// (Core/Scripting/ScriptBindings.h) before the backends build their states;
/// they act on the running App's active scene and view.
///
///   world.find(name)  -> the active scene's entity named `name`, or nil
///   world.viewSize()  -> vec2 of the presented view in pixels
YA_GAME_RUNTIME_API void registerGameplayScriptFunctions();

} // namespace ya
