
#pragma once

#include "Core/Base.h"
#include "Core/Reflection/Reflection.h"

#include "Runtime/Application/App.h"
#include "Scene/SceneManager.h"

#include <algorithm>
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
        sol::object value;          // 当前值
        std::string typeHint;       // "float", "int", "bool", "string", "Vec3" 等
        float       min     = 0.0f; // 数值范围（可选）
        float       max     = 100.0f;
        std::string tooltip = ""; // 提示信息

        // 序列化值（用于持久化和运行时应用）
        std::string serializedValue = "";
    };

    struct ScriptInstance
    {
        std::string scriptPath;
        bool        bLoaded                 = false;
        bool        bAuthoringPreviewAttempted = false;

        sol::table self;
        // 注：不再使用独立环境，所有脚本共享全局环境以支持 require()

        // 生命周期回调
        sol::function onInit;
        sol::function onUpdate;
        sol::function onDestroy;
        sol::function onEnable;
        sol::function onDisable;

        // 可编辑属性（从 Lua 表中提取）
        std::vector<ScriptProperty> properties;

        // 属性覆盖值（编辑器修改后的值，运行时应用）
        // Key: 属性名, Value: std::any 存储的实际值
        std::unordered_map<std::string, std::any> propertyOverrides;

        bool enabled = true;

        // 刷新属性列表（从 self 表中读取）
        void refreshProperties();

        // 应用属性覆盖（在运行时初始化后调用）
        void applyPropertyOverrides(sol::state &lua);

      private:
        // 类型推断辅助函数
        static std::string inferType(const sol::object &value);
    };

    std::vector<ScriptInstance> scripts;

    // Unity-like API
    ScriptInstance *addScript(const std::string &path)
    {
        scripts.push_back({path});
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

    /**
     * @brief Cleanup all Lua references safely
     * Should be called before lua state is destroyed
     */
    void cleanup()
    {
        for (auto& script : scripts) {
            script.properties.clear();
            script.propertyOverrides.clear();
            // Release sol references explicitly
            script.self = sol::lua_nil;
            script.onInit = sol::lua_nil;
            script.onUpdate = sol::lua_nil;
            script.onDestroy = sol::lua_nil;
            script.onEnable = sol::lua_nil;
            script.onDisable = sol::lua_nil;
        }
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
            s["scriptPath"] = script.scriptPath;
            s["enabled"]    = script.enabled;

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
            script.scriptPath = sourceScript.scriptPath;
            script.enabled = sourceScript.enabled;
            script.propertyOverrides = sourceScript.propertyOverrides;
        }
    }
};



} // namespace ya
