#include "Script/Lua/LuaBackend.h"

#include "Core/Scripting/ScriptBindings.h"
#include "ECS/Systems/ScriptPropertyPreview.h"
#include "ECS/Systems/WorldScriptRuntime.h"
#include "Script/Lua/LuaScriptingSystem.h"
#include "Script/Lua/ScriptPropertyPreview.h"

namespace ya
{
namespace
{

IWorldScriptRuntime* createLuaWorldScriptRuntime()
{
    return new LuaScriptingSystem();
}

struct LuaScriptCallbackBridge final : script::IScriptCallbacks
{
    void retain(script::ScriptFnHandle fn) override { LuaScriptingSystem::retainFunction(fn.id); }

    void release(script::ScriptFnHandle fn) override { LuaScriptingSystem::releaseFunction(fn.id); }

    void invoke(script::ScriptFnHandle fn, script::ScriptSelfHandle self, script::ScriptArgs args) override
    {
        LuaScriptingSystem::invokeFunction(fn.id, self.runtimeId, args);
    }
};

LuaScriptCallbackBridge& callbackBridge()
{
    // No sol members. The functions it calls live on each LuaScriptingSystem
    // and die with that system's state.
    static LuaScriptCallbackBridge bridge;
    return bridge;
}

} // namespace

void installLuaScriptBackend()
{
    setWorldScriptRuntimeFactory(&createLuaWorldScriptRuntime);
    script::setScriptCallbacks(&callbackBridge());
    // One preview for the process. It owns its own sol::state. App teardown
    // does not destroy it: a test process and the editor construct more than
    // one App, and clearing the pointer on the first quit would drop it for
    // the next. A function-local static would destroy that state during
    // static teardown; this heap object is intentionally not freed.
    if (!scriptPropertyPreview()) {
        setScriptPropertyPreview(createScriptPropertyPreview());
    }
}

} // namespace ya
