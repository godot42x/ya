#pragma once

#include "Core/Api.h"
#include "Core/Scripting/ScriptValue.h"

#include <string_view>

namespace ya
{

struct Entity;
struct IWorldScriptRuntime;

/// Install the Lua world-script runtime and the editor property preview.
/// Call once, from the process entry (YARuntime and the test runner), before
/// any App is constructed. A missing call leaves gameplay scripts off and
/// the inspector without property rows. The preview outlives every App in
/// the process; `uninstallLuaScriptBackend` is the matching teardown.
YA_SCRIPT_LUA_API void installLuaScriptBackend();

/// Drops the factory, the callback bridge, and the property preview installed
/// above. Call from the same process entry, after the last App is gone and
/// before static teardown: the preview owns a `sol::state`, which must not
/// die in a static destructor.
YA_SCRIPT_LUA_API void uninstallLuaScriptBackend();

/// RAII for `installLuaScriptBackend` / `uninstallLuaScriptBackend`.
/// One per process. Declare it in the process entry so the preview outlives
/// every App and is destroyed while Lua is still alive, before static teardown.
struct LuaScriptBackendScope
{
    LuaScriptBackendScope() { installLuaScriptBackend(); }
    ~LuaScriptBackendScope() { uninstallLuaScriptBackend(); }

    LuaScriptBackendScope(const LuaScriptBackendScope&)            = delete;
    LuaScriptBackendScope& operator=(const LuaScriptBackendScope&) = delete;
};

/// The `Input` usertype is built after module functions are projected, so it
/// replaces the `Input` table. Put the registered `Input.setKeyHandler` back
/// on that type. Colon calls pass the userdata first; the closure drops it.
/// No-op when the function was never registered or this runtime is not Lua.
YA_SCRIPT_LUA_API void attachInputSetKeyHandler(IWorldScriptRuntime& runtime);

/// First loaded script on `entity` (optionally the one whose path normalizes
/// to `path`). Rebinds `self.entity` the same way `entity:call` does, then
/// returns that live table's id. `runtimeId == 0` when nothing matches.
/// A non-Lua runtime returns an empty handle.
[[nodiscard]] YA_SCRIPT_LUA_API script::ScriptSelfHandle entityScriptSelf(IWorldScriptRuntime& runtime,
                                                                          Entity&              entity,
                                                                          std::string_view     path);

/// `entity:call(name, ...)`. Idempotent: the first registration wins, a later
/// call does not warn. The body is `callEntityScript`.
YA_SCRIPT_LUA_API void registerEntityScriptCall();

/// Call a held function with no `self` and return its first value. Errors are
/// logged and come back empty. `IScriptCallbacks::invoke` drops the return;
/// the key handler needs the bool.
[[nodiscard]] YA_SCRIPT_LUA_API script::ScriptValue callHeldScriptFunction(IWorldScriptRuntime&     runtime,
                                                                           script::ScriptFnHandle   fn,
                                                                           script::ScriptArgs       args);

} // namespace ya
