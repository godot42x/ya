#pragma once

#include "Core/Api.h"

#include <string>

namespace ya
{

struct Scene;

/// Gameplay module functions every script backend projects (`world.find` in
/// Lua, `ya.world.find` in JS). Registered through the script export
/// (Core/Scripting/ScriptBindings.h) before the backends build their states;
/// they act on the running App's active scene and view.
///
///   world.find(name)  -> the active scene's entity named `name`, or nil
///   world.viewSize()  -> vec2 of the presented view in pixels
///   world.loadScene(path, spawnName) -> switch scenes at the frame-end
///       structural flush, then place the entity named "Player" on the spawn
///       marker `spawnName` (rpg R3)
YA_GAME_RUNTIME_API void registerGameplayScriptFunctions();

/// Places the scene's "Player" entity on the spawn marker named `spawnName`
/// (a Node3D at scene root; only its position is taken). A missing marker or
/// player logs a warning and leaves the scene as authored — the gameplay
/// actor's name is the contract, nothing else is hardwired.
YA_GAME_RUNTIME_API void placePlayerAtSpawn(Scene& scene, const std::string& spawnName);

} // namespace ya
