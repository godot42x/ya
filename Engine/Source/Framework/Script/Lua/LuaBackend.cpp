#include "Script/Lua/LuaBackend.h"

#include "Core/Log.h"
#include "Core/Scripting/ScriptBindings.h"
#include "ECS/Entity.h"
#include "ECS/Systems/Components/LuaScriptComponent.h"
#include "ECS/Systems/ScriptPropertyPreview.h"
#include "ECS/Systems/WorldScriptRuntime.h"
#include "Scene/Core/SceneScriptBindings.h"
#include "Script/Lua/LuaScriptBinding.h"
#include "Script/Lua/LuaScriptingSystem.h"
#include "Script/Lua/ScriptPropertyPreview.h"

#include <vector>

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

namespace
{

/// `input:setKeyHandler(fn)` passes the Input userdata first. A dot call on
/// the type passes the handler alone. Anything that is not a function clears
/// the handler, matching the previous sol registration.
int inputSetKeyHandler(lua_State* L)
{
    const auto* function = static_cast<const script::ScriptModuleFunction*>(lua_touserdata(L, lua_upvalueindex(1)));
    if (!function || !function->fn) {
        lua_pushstring(L, "Input.setKeyHandler is not registered");
        return lua_error(L);
    }
    int       index = 1;
    const int first = lua_type(L, 1);
    if (first == LUA_TUSERDATA || first == LUA_TTABLE) {
        index = 2;
    }
    try {
        std::vector<script::ScriptValue> args;
        if (lua_type(L, index) == LUA_TFUNCTION) {
            args.push_back(toScriptValue(L, index));
        }
        function->fn(args);
        return 0;
    }
    catch (const std::exception& e) {
        lua_pushstring(L, e.what());
    }
    return lua_error(L);
}

} // namespace

void attachInputSetKeyHandler(IWorldScriptRuntime& runtime)
{
    auto* system = dynamic_cast<LuaScriptingSystem*>(&runtime);
    if (!system) {
        return;
    }
    const script::ScriptModuleFunction* target = nullptr;
    script::forEachModuleFunction([&](const script::ScriptModuleFunction& function) {
        if (function.module == "Input" && function.name == "setKeyHandler") {
            target = &function;
        }
    });
    if (!target) {
        return;
    }
    lua_State* L = system->lua().lua_state();
    if (lua_getglobal(L, "Input") != LUA_TTABLE) {
        lua_pop(L, 1);
        YA_CORE_ERROR("attachInputSetKeyHandler: Input is not a table");
        return;
    }
    lua_pushlightuserdata(L, const_cast<script::ScriptModuleFunction*>(target));
    lua_pushcclosure(L, &inputSetKeyHandler, 1);
    lua_setfield(L, -2, "setKeyHandler");
    lua_pop(L, 1);
}

script::ScriptSelfHandle entityScriptSelf(IWorldScriptRuntime& runtime, Entity& entity, std::string_view path)
{
    auto* system = dynamic_cast<LuaScriptingSystem*>(&runtime);
    if (!system) {
        return {};
    }
    auto* component = entity.hasComponent<LuaScriptComponent>() ? entity.getComponent<LuaScriptComponent>() : nullptr;
    if (!component) {
        return {};
    }
    const std::string wanted = path.empty() ? std::string{} : ScriptInstance::normalizeScriptPath(path);
    for (ScriptInstance& script : component->scripts) {
        if (!script.bLoaded || script.runtimeId == 0) {
            continue;
        }
        if (!wanted.empty() && ScriptInstance::normalizeScriptPath(script.scriptPath) != wanted) {
            continue;
        }
        sol::table self = system->selfOf(script.runtimeId);
        if (!self.valid()) {
            continue;
        }
        self["entity"] = LuaScriptObject{script::entityRef(&entity)};
        return script::ScriptSelfHandle{.runtimeId = script.runtimeId};
    }
    return {};
}

script::ScriptValue callHeldScriptFunction(IWorldScriptRuntime& runtime, script::ScriptFnHandle fn, script::ScriptArgs args)
{
    auto* system = dynamic_cast<LuaScriptingSystem*>(&runtime);
    if (!system || fn.id == 0) {
        return {};
    }
    lua_State* L = system->lua().lua_state();
    LuaScriptingSystem::pushHeldFunction(L, fn.id);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        YA_CORE_ERROR("script function {} is gone", fn.id);
        return {};
    }
    for (const script::ScriptValue& value : args) {
        pushLuaValue(L, value);
    }
    if (lua_pcall(L, static_cast<int>(args.size()), 1, 0) != LUA_OK) {
        const char* message = lua_tostring(L, -1);
        YA_CORE_ERROR("script function {} error: {}", fn.id, message ? message : "");
        lua_pop(L, 1);
        return {};
    }
    script::ScriptValue result;
    try {
        result = toScriptValue(L, -1);
    }
    catch (const std::exception& e) {
        YA_CORE_ERROR("script function {} return: {}", fn.id, e.what());
    }
    lua_pop(L, 1);
    return result;
}

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
