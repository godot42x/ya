
#pragma once

#include "Core/Base.h"
#include "Core/Reflection/Reflection.h"

#include "Core/Api.h"
#include "ECS/Component.h"

#include <algorithm>
#include <any>
#include <optional>
#include <sol/sol.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace ya
{

struct LuaScriptComponent : public IComponent
{
    // Registration-only: the scripts vector uses custom UI rendering (DetailsView.Components.Basic.cpp)
    // because sol::object / sol::function / std::any cannot be trivially reflected.
    YA_REFLECT_BEGIN(LuaScriptComponent)
    YA_REFLECT_END()

    struct ScriptProperty
    {
        std::string name;
        std::any    value;
        std::string typeHint;
        float       min     = 0.0f;
        float       max     = 100.0f;
        std::string tooltip = "";
        std::string serializedValue = "";
    };

    struct ScriptInstance
    {
        std::string scriptPath;
        bool        bLoaded                    = false;
        bool        bAuthoringPreviewAttempted = false;
        bool        bAuthoringPreviewLoaded    = false;

        sol::table self;

        sol::function onInit;
        sol::function onStart;
        sol::function onUpdate;
        sol::function onDestroy;
        sol::function onEnable;
        sol::function onDisable;

        std::vector<ScriptProperty> properties;
        std::unordered_map<std::string, std::any> propertyOverrides;

        bool enabled = true;

        /// `executionOrder` declared by the script table; read on load, not serialized.
        int                scriptExecutionOrder = 0;
        /// Per-instance override, serialized only when set.
        std::optional<int> executionOrderOverride;

        /// Lower runs first. Ties fall back to scene-tree order.
        [[nodiscard]] int executionOrder() const { return executionOrderOverride.value_or(scriptExecutionOrder); }

        YA_ECS_SYSTEMS_API void refreshProperties();
        YA_ECS_SYSTEMS_API void capturePropertiesFrom(sol::table table);
        YA_ECS_SYSTEMS_API void applyPropertyOverrides(sol::state& lua);
        YA_ECS_SYSTEMS_API void applyPropertyOverridesTo(sol::table table, sol::state& lua);
        /// Drop sol handles while their lua_State is still alive. Keeps path,
        /// enabled, propertyOverrides, and captured C++ property rows.
        YA_ECS_SYSTEMS_API void releaseLuaHandles();

        static YA_ECS_SYSTEMS_API std::string normalizeScriptPath(std::string_view path);
    };

    std::vector<ScriptInstance> scripts;

    // Unity-like API
    ScriptInstance *addScript(const std::string &path)
    {
        scripts.push_back({.scriptPath = ScriptInstance::normalizeScriptPath(path)});
        return &scripts.back();
    }

    ScriptInstance *attachScript(const std::string &path)
    {
        return addScript(path);
    }

    ScriptInstance *getScript(const std::string &path)
    {
        auto it = std::find_if(scripts.begin(), scripts.end(), [&](auto &s) { return s.scriptPath == path; });
        return it != scripts.end() ? &(*it) : nullptr;
    }

    void removeScript(const std::string &path)
    {
        scripts.erase(std::remove_if(scripts.begin(), scripts.end(), [&](auto &s) { return s.scriptPath == path; }),
                      scripts.end());
    }

    void releaseLuaHandles()
    {
        for (auto& script : scripts) {
            script.releaseLuaHandles();
        }
    }

    /// Drop Lua handles then discard every script row. Call only when the
    /// component itself is going away, not when pausing play.
    void cleanup()
    {
        releaseLuaHandles();
        scripts.clear();
    }

    // Scripts own their complete serialized representation because the runtime
    // Lua handles are intentionally excluded from scene data.
    bool useReflectionSerialization() const override { return false; }

    void serializeCustom(nlohmann::json& out) const override
    {
        auto scriptsJson = nlohmann::json::array();
        for (const auto& script : scripts) {
            nlohmann::json s;
            s["scriptPath"] = ScriptInstance::normalizeScriptPath(script.scriptPath);
            s["enabled"]    = script.enabled;
            if (script.executionOrderOverride) {
                s["executionOrder"] = *script.executionOrderOverride;
            }

            if (!script.propertyOverrides.empty()) {
                nlohmann::json overrides = nlohmann::json::object();
                for (const auto& [propName, value] : script.propertyOverrides) {
                    if (value.type() == typeid(int))
                        overrides[propName] = std::any_cast<int>(value);
                    else if (value.type() == typeid(float))
                        overrides[propName] = std::any_cast<float>(value);
                    else if (value.type() == typeid(double))
                        overrides[propName] = std::any_cast<double>(value);
                    else if (value.type() == typeid(bool))
                        overrides[propName] = std::any_cast<bool>(value);
                    else if (value.type() == typeid(std::string))
                        overrides[propName] = std::any_cast<std::string>(value);
                    else if (value.type() == typeid(glm::vec2)) {
                        auto vec = std::any_cast<glm::vec2>(value);
                        overrides[propName] = nlohmann::json::array({vec.x, vec.y});
                    } else if (value.type() == typeid(glm::vec3)) {
                        auto vec = std::any_cast<glm::vec3>(value);
                        overrides[propName] = nlohmann::json::array({vec.x, vec.y, vec.z});
                    } else if (value.type() == typeid(glm::vec4)) {
                        auto vec = std::any_cast<glm::vec4>(value);
                        overrides[propName] = nlohmann::json::array({vec.x, vec.y, vec.z, vec.w});
                    }
                }
                s["propertyOverrides"] = std::move(overrides);
            }
            scriptsJson.push_back(std::move(s));
        }
        out["scripts"] = std::move(scriptsJson);
    }

    void deserializeCustom(const nlohmann::json& in) override
    {
        scripts.clear();
        if (!in.contains("scripts") || !in.at("scripts").is_array()) return;

        for (const auto& scriptJson : in.at("scripts")) {
            auto* script = addScript(scriptJson.value("scriptPath", ""));
            if (!script) continue;

            script->enabled = scriptJson.value("enabled", true);
            if (scriptJson.contains("executionOrder") && scriptJson.at("executionOrder").is_number_integer()) {
                script->executionOrderOverride = scriptJson.at("executionOrder").get<int>();
            }
            if (!scriptJson.contains("propertyOverrides") || !scriptJson.at("propertyOverrides").is_object())
                continue;

            for (auto& [propName, valueJson] : scriptJson.at("propertyOverrides").items()) {
                if (valueJson.is_boolean())
                    script->propertyOverrides[propName] = valueJson.get<bool>();
                else if (valueJson.is_number_integer())
                    script->propertyOverrides[propName] = valueJson.get<int>();
                else if (valueJson.is_number_float())
                    script->propertyOverrides[propName] = valueJson.get<float>();
                else if (valueJson.is_string())
                    script->propertyOverrides[propName] = valueJson.get<std::string>();
                else if (valueJson.is_array() && valueJson.size() == 2)
                    script->propertyOverrides[propName] = glm::vec2(valueJson[0].get<float>(), valueJson[1].get<float>());
                else if (valueJson.is_array() && valueJson.size() == 3)
                    script->propertyOverrides[propName] = glm::vec3(valueJson[0].get<float>(), valueJson[1].get<float>(), valueJson[2].get<float>());
                else if (valueJson.is_array() && valueJson.size() == 4)
                    script->propertyOverrides[propName] = glm::vec4(valueJson[0].get<float>(), valueJson[1].get<float>(), valueJson[2].get<float>(), valueJson[3].get<float>());
            }
        }
    }

    void cloneCustom(const IComponent& src) override
    {
        const auto& srcLua = static_cast<const LuaScriptComponent&>(src);
        scripts.clear();
        scripts.reserve(srcLua.scripts.size());
        for (const auto& sourceScript : srcLua.scripts) {
            auto& script = scripts.emplace_back();
            script.scriptPath = ScriptInstance::normalizeScriptPath(sourceScript.scriptPath);
            script.enabled = sourceScript.enabled;
            script.executionOrderOverride = sourceScript.executionOrderOverride;
            script.propertyOverrides = sourceScript.propertyOverrides;
        }
    }
};



} // namespace ya
