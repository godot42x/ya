#pragma once

#include "Core/Api.h"
#include "GameRuntime/Script/GameplayScriptFunctions.h"

namespace ya
{

struct GameUIHost;
struct IWorldScriptRuntime;

/// After the world script runtime's `init`: put `Input.setKeyHandler` back on
/// the Input usertype (init replaces the projected module table) and install
/// the widget script runtime.
///
/// `world.*`, `Input.setKeyHandler` and `ui.*` are module functions registered
/// before `init` via `registerGameplayScriptFunctions`. `ctx` is unused here;
/// callers that already pass the same context keep compiling.
YA_GAME_RUNTIME_API void bindGameplayLua(IWorldScriptRuntime& runtime, GameUIHost& ui, GameplayScriptContext ctx = {});

} // namespace ya
