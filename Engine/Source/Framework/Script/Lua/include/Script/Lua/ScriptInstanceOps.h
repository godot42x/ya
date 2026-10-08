#pragma once

// Sol-free operations on a live script instance. Implemented by the Lua
// backend. GameRuntime calls these and does not include the Lua system header.

#include "Core/Scripting/ScriptValue.h"
#include "ECS/Systems/WorldScriptRuntime.h"

#include <cstdint>
#include <memory>
#include <string>

namespace ya
{

struct ScriptInstance;

enum class EScriptCallback : uint8_t
{
    Init,
    Start,
    Update,
    Destroy,
};

YA_SCRIPT_LUA_API bool loadScriptInstance(IWorldScriptRuntime& runtime, ScriptInstance& instance,
                                          std::unique_ptr<IScriptInstanceHost> host);
YA_SCRIPT_LUA_API bool callScriptInstance(IWorldScriptRuntime& runtime, ScriptInstance& instance,
                                          EScriptCallback which, float deltaTime = 0.0f);
/// `bSwallowErrors`: log a failing target and return nil. Otherwise raise ScriptError.
YA_SCRIPT_LUA_API script::ScriptValue callScriptInstanceNamed(IWorldScriptRuntime& runtime, ScriptInstance& instance,
                                                              const std::string& name, script::ScriptArgs args,
                                                              bool bSwallowErrors);
YA_SCRIPT_LUA_API void destroyScriptInstance(IWorldScriptRuntime& runtime, ScriptInstance& instance);
/// `name()` on each loaded world script, in execution order. True when one returns true.
YA_SCRIPT_LUA_API bool callLoadedWorldScripts(IWorldScriptRuntime& runtime, const std::string& name);

/// Shared owner of the listeners a live instance added. Empty when the id is not live.
/// Delegates hold it weakly, so dropping the instance ends those listeners.
[[nodiscard]] YA_SCRIPT_LUA_API std::shared_ptr<void> scriptInstanceListenerOwner(uint64_t runtimeId);
/// `retain` the function on that instance. Released when its self is replaced or destroyed.
YA_SCRIPT_LUA_API bool retainInstanceFunction(uint64_t runtimeId, script::ScriptFnHandle fn);
[[nodiscard]] YA_SCRIPT_LUA_API bool scriptFunctionAlive(script::ScriptFnHandle fn);

} // namespace ya
