
#pragma once

#include "Core/Base.h"

#include "Core/App/App.h"
#include "Scene/SceneManager.h"

#include <sol/sol.hpp>
#include <vector>
#include <string>
#include <algorithm>
#include <unordered_map>

namespace ya
{

struct LuaScriptComponent : public IComponent
{
    YA_ECS_COMPONENT(LuaScriptComponent)

    struct ScriptProperty
    {
        std::string name;
        sol::object value;       // 当前值
        std::string typeHint;    // "float", "int", "bool", "string", "Vec3" 等
        float       min = 0.0f;  // 数值范围（可选）
        float       max = 100.0f;
        std::string tooltip = "";  // 提示信息
        
        // 序列化值（用于持久化和运行时应用）
        std::string serializedValue = "";
    };

    struct ScriptInstance
    {
        std::string scriptPath;
        bool        bLoaded = false;
        bool        bEditorPreviewAttempted = false;  // 编辑器预览是否已尝试加载

        sol::table       self;
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
        void applyPropertyOverrides(sol::state& lua);
        
      private:
        // 类型推断辅助函数
        static std::string inferType(const sol::object &value);
    };

    std::vector<ScriptInstance> scripts;

    // Unity-like API
    ScriptInstance* addScript(const std::string& path)
    {
        scripts.push_back({path});
        return &scripts.back();
    }

    ScriptInstance* getScript(const std::string& path)
    {
        auto it = std::find_if(scripts.begin(), scripts.end(),
                               [&](auto &s) { return s.scriptPath == path; });
        return it != scripts.end() ? &(*it) : nullptr;
    }

    void removeScript(const std::string& path)
    {
        scripts.erase(std::remove_if(scripts.begin(), scripts.end(),
                                     [&](auto &s) { return s.scriptPath == path; }),
                      scripts.end());
    }

    // 兼容旧 API（使用第一个脚本）
    [[deprecated("Use scripts vector directly")]]
    std::string getScriptPath() const { return scripts.empty() ? "" : scripts[0].scriptPath; }
    
    [[deprecated("Use addScript() instead")]]
    void setScriptPath(const std::string& path) { 
        if (scripts.empty()) addScript(path);
        else scripts[0].scriptPath = path;
    }

    static void registerReflection()
    {
        Register<LuaScriptComponent>("LuaScriptComponent");
        // TODO: 序列化 scripts 数组
    }
};



} // namespace ya