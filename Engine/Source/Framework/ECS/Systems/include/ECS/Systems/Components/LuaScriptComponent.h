
#pragma once

#include "Core/Base.h"
#include "Core/Reflection/Reflection.h"
#include "Core/Reflection/ValueFacets.h"

#include "Core/Api.h"
#include "ECS/Component.h"
#include "ECS/Systems/ScriptInstance.h"

#include <algorithm>
#include <string>
#include <vector>

namespace ya
{

struct LuaScriptComponent : public IComponent
{
    // Registration-only. Live handles do not enter reflection; scripts are
    // serialized by hand instead of as a reflected property list.
    YA_REFLECT_BEGIN(LuaScriptComponent)
    YA_REFLECT_END()

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

    /// Drop each row's live table when `runtimeId != 0`, without running
    /// onDestroy. Preview rows never receive an id, so this is a no-op for
    /// them. The play runtime's side table owns the handles; clearing an id
    /// here releases that entry while its state is still alive.
    YA_ECS_SYSTEMS_API void releaseLuaHandles();

    /// Drop live tables then discard every script row. Call only when the
    /// component itself is going away, not when pausing play.
    void cleanup()
    {
        releaseLuaHandles();
        scripts.clear();
    }

    // Scripts own their complete serialized representation because the runtime
    // Lua handles are intentionally excluded from scene data.
    void serializeCustom(nlohmann::json& out) const
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
                    if (const auto* integer = script::scriptIf<int64_t>(value))
                        overrides[propName] = *integer;
                    else if (const auto* number = script::scriptIf<double>(value))
                        overrides[propName] = *number;
                    else if (const auto* flag = script::scriptIf<bool>(value))
                        overrides[propName] = *flag;
                    else if (const auto* text = script::scriptIf<std::string>(value))
                        overrides[propName] = *text;
                    else if (const auto* vec = script::scriptIf<glm::vec2>(value))
                        overrides[propName] = nlohmann::json::array({vec->x, vec->y});
                    else if (const auto* vec = script::scriptIf<glm::vec3>(value))
                        overrides[propName] = nlohmann::json::array({vec->x, vec->y, vec->z});
                    else if (const auto* vec = script::scriptIf<glm::vec4>(value))
                        overrides[propName] = nlohmann::json::array({vec->x, vec->y, vec->z, vec->w});
                }
                s["propertyOverrides"] = std::move(overrides);
            }
            scriptsJson.push_back(std::move(s));
        }
        out["scripts"] = std::move(scriptsJson);
    }

    void deserializeCustom(const nlohmann::json& in)
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
                    script->propertyOverrides[propName] = script::scriptMake(valueJson.get<bool>());
                else if (valueJson.is_number_integer())
                    script->propertyOverrides[propName] = script::scriptMake(valueJson.get<int64_t>());
                else if (valueJson.is_number_float())
                    script->propertyOverrides[propName] = script::scriptMake(valueJson.get<double>());
                else if (valueJson.is_string())
                    script->propertyOverrides[propName] = script::scriptMake(valueJson.get<std::string>());
                else if (valueJson.is_array() && valueJson.size() == 2)
                    script->propertyOverrides[propName] = script::scriptMake(glm::vec2(valueJson[0].get<float>(), valueJson[1].get<float>()));
                else if (valueJson.is_array() && valueJson.size() == 3)
                    script->propertyOverrides[propName] = script::scriptMake(glm::vec3(valueJson[0].get<float>(), valueJson[1].get<float>(), valueJson[2].get<float>()));
                else if (valueJson.is_array() && valueJson.size() == 4)
                    script->propertyOverrides[propName] = script::scriptMake(glm::vec4(valueJson[0].get<float>(), valueJson[1].get<float>(), valueJson[2].get<float>(), valueJson[3].get<float>()));
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

namespace {

struct LuaComponentJsonRegistrar
{
    LuaComponentJsonRegistrar()
    {
        LuaScriptComponent::__ensure_reflection_registered();
        reflection::deferStaticInit([] {
            refl::TypeRegistry::instance().setFacet<reflection::FComponentJsonFacet>(
                type_index_v<LuaScriptComponent>,
                reflection::FComponentJsonFacet{
                    .bUseReflection = false,
                    .serialize = [](const void* component, nlohmann::json& out) {
                        static_cast<const LuaScriptComponent*>(component)->serializeCustom(out);
                    },
                    .deserialize = [](void* component, const nlohmann::json& in) {
                        static_cast<LuaScriptComponent*>(component)->deserializeCustom(in);
                    },
                });
        });
    }
};

const LuaComponentJsonRegistrar kLuaComponentJsonRegistrar{};

} // namespace

} // namespace ya
