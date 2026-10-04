#pragma once

#include "Core/Api.h"
#include "GameRuntime/Script/GameplayScriptFunctions.h"

namespace ya
{

struct GameUIHost;
struct LuaScriptingSystem;

/// Gameplay Lua surface for the 2D sample: sprite read/write, sprite spawn,
/// and the mounted-document text/visibility bridge. Registered by the host
/// after `LuaScriptingSystem::init`. GUI widgets stay free of Lua; this file
/// lives in GameRuntime because that is the module that already sees both
/// the script state and `Sprite2DComponent`.
///
/// `ctx` is the same context passed to `registerGameplayScriptFunctions`.
/// `spawnSprite` / `destroyEntity` use its scene, `viewAspect` uses
/// `gameplayPresentedView` (the same read as `world.viewSize`), and
/// `Input.setKeyHandler` captures `ctx.inputRouter` at bind time. An empty
/// context leaves those calls with no scene and no key router.
/// `world.spawnSprite(name [, texturePath])`: a missing or blank path keeps
/// the probe texture; a path goes through `TextureSlot::fromPath`.
YA_GAME_RUNTIME_API void bindGameplayLua(LuaScriptingSystem& scripting, GameUIHost& ui, GameplayScriptContext ctx = {});

} // namespace ya
