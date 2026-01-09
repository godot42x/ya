
#pragma once

#include "Core/Base.h"

#include "Core/App/App.h"
#include "Scene/SceneManager.h"

#include <algorithm>
#include <sol/sol.hpp>
#include <string>
#include <unordered_map>
#include <vector>


// 反射系统已迁移到 UnifiedReflection.h
// #include "Core/Reflection/UnifiedReflection.h"

namespace ya
{

struct LuaScriptComponent : public IComponent
{
    // TODO: 序列化 scripts 数组需要自定义处理
    // YA_REFLECT(LuaScriptComponent,
    //     PROP(scripts).Tooltip("Attached Lua scripts"))
    // )

    // YA_REFLECT_WITH_META(LuaScriptComponent,
    //                      PROP(scripts, .EditAnywhere().Tooltip("Attached Lua scripts")));

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
        bool        bEditorPreviewAttempted = false; // 编辑器预览是否已尝试加载

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

    ~LuaScriptComponent()
    {
        // Destructor - references should already be cleared by cleanup()
        // If not, they may crash if lua state is already destroyed
    }
};



} // namespace ya