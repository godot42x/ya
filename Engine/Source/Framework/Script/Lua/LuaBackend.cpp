#include "Script/Lua/LuaBackend.h"

#include "Core/Log.h"
#include "Core/Scripting/ScriptBindings.h"
#include "ECS/Entity.h"
#include "ECS/Systems/Components/LuaScriptComponent.h"
#include "ECS/Systems/ScriptInstance.h"
#include "ECS/Systems/ScriptPropertyPreview.h"
#include "ECS/Systems/UILogicAuthoring.h"
#include "ECS/Systems/WorldScriptRuntime.h"
#include "Scene/Core/SceneScriptBindings.h"
#include "Script/Lua/LuaScriptBinding.h"
#include "Script/Lua/LuaScriptingSystem.h"
#include "Script/Lua/ScriptPropertyPreview.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace ya
{
namespace
{

IWorldScriptRuntime* createLuaWorldScriptRuntime()
{
    return new LuaScriptingSystem();
}

struct LuaLogicAuthoring final : IUILogicAuthoring
{
    void describe(std::string path, std::string& type, nlohmann::json& data) const override
    {
        type = "script.lua";
        data = nlohmann::json{{"script", ScriptInstance::normalizeScriptPath(std::move(path))}};
    }

    bool claims(std::string_view type) const override { return type == "script.lua"; }

    std::string pathOf(std::string_view type, const nlohmann::json& data) const override
    {
        if (type != "script.lua" || !data.is_object()) {
            return {};
        }
        const auto it = data.find("script");
        if (it == data.end() || !it->is_string()) {
            return {};
        }
        return it->get<std::string>();
    }

    bool loadPreview(std::string path, ScriptInstance& instance) const override
    {
        instance.scriptPath = ScriptInstance::normalizeScriptPath(std::move(path));
        IScriptPropertyPreview* preview = scriptPropertyPreview();
        return preview && preview->load(instance);
    }
};

LuaLogicAuthoring* gLogicAuthoring = nullptr;

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

void installLuaScriptBackend()
{
    // One preview for the process. It owns its own sol::state and outlives
    // every App: a test process and the editor construct more than one.
    // `uninstallLuaScriptBackend` destroys it. A function-local static would
    // destroy that state during static teardown, after Lua is already gone.
    YA_CORE_ASSERT(scriptPropertyPreview() == nullptr && script::scriptCallbacks() == nullptr &&
                       uiLogicAuthoring() == nullptr,
                   "installLuaScriptBackend: already installed");
    setWorldScriptRuntimeFactory(&createLuaWorldScriptRuntime);
    script::setScriptCallbacks(&callbackBridge());
    setScriptPropertyPreview(createScriptPropertyPreview());
    gLogicAuthoring = new LuaLogicAuthoring();
    setUILogicAuthoring(gLogicAuthoring);
}

void uninstallLuaScriptBackend()
{
    if (IScriptPropertyPreview* preview = scriptPropertyPreview()) {
        setScriptPropertyPreview(nullptr);
        destroyScriptPropertyPreview(preview);
    }
    script::setScriptCallbacks(nullptr);
    setWorldScriptRuntimeFactory(nullptr);
    setUILogicAuthoring(nullptr);
    delete gLogicAuthoring;
    gLogicAuthoring = nullptr;
}

} // namespace ya
