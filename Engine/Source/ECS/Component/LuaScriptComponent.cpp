#include "LuaScriptComponent.h"
#include <cmath>
#include <glm/glm.hpp>
#include <limits>


namespace ya
{

void LuaScriptComponent::ScriptInstance::refreshProperties()
{
    properties.clear();
    if (!self.valid()) return;

    // ============================================
    // 方案1：检查 _PROPERTIES 元数据表
    // ============================================
    sol::optional<sol::table> propsTable = self["_PROPERTIES"];
    if (propsTable)
    {
        for (const auto &pair : *propsTable)
        {
            sol::object key   = pair.first;
            sol::object value = pair.second;

            if (!key.is<std::string>()) continue;

            std::string    propName = key.as<std::string>();
            ScriptProperty prop;
            prop.name = propName;

            // 如果是表结构（包含元数据）
            if (value.is<sol::table>())
            {
                sol::table propDef = value.as<sol::table>();

                // 读取 value 字段
                sol::optional<sol::object> propValue = propDef["value"];
                if (propValue)
                {
                    prop.value = *propValue;

                    // 【优化】自动将 _PROPERTIES 中的默认值写入脚本表
                    // 这样 Lua 脚本中就不需要重复定义 Script.radius = 5.0
                    sol::object currentValue = self[propName];
                    if (!currentValue.valid() || currentValue.get_type() == sol::type::nil)
                    {
                        self[propName] = *propValue;
                    }
                    else
                    {
                        // 如果脚本已经定义了该字段，使用脚本的值（支持覆盖）
                        prop.value = currentValue;
                    }

                    // 读取类型提示
                    sol::optional<std::string> typeHint = propDef["type"];
                    // TODO: 类型推断
                    prop.typeHint = typeHint.value_or("unknown");

                    // 读取范围
                    sol::optional<float> minVal = propDef["min"];
                    sol::optional<float> maxVal = propDef["max"];
                    prop.min                    = minVal.value_or(0.0f);
                    prop.max                    = maxVal.value_or(100.0f);

                    // 读取提示信息
                    sol::optional<std::string> tooltip = propDef["tooltip"];
                    prop.tooltip                       = tooltip.value_or("");

                    properties.push_back(prop);
                }
            }
        }
        return; // 找到元数据表，直接返回
    }
}

void LuaScriptComponent::ScriptInstance::applyPropertyOverrides(sol::state &lua)
{
    if (!self.valid() || propertyOverrides.empty()) return;

    YA_CORE_INFO("[LuaScript] Applying {} property overrides to {}",
                 propertyOverrides.size(),
                 scriptPath);

    for (const auto &[propName, anyValue] : propertyOverrides)
    {
        if (!anyValue.has_value())
        {
            YA_CORE_WARN("[LuaScript] Property '{}' has no value, skipping", propName);
            continue;
        }

        try
        {
            // 直接从 std::any 转换为 sol::object
            // 根据类型分别处理
            sol::object luaValue = sol::nil;

            if (anyValue.type() == typeid(int))
            {
                luaValue = sol::make_object(lua, std::any_cast<int>(anyValue));
            }
            else if (anyValue.type() == typeid(float))
            {
                luaValue = sol::make_object(lua, std::any_cast<float>(anyValue));
            }
            else if (anyValue.type() == typeid(double))
            {
                luaValue = sol::make_object(lua, std::any_cast<double>(anyValue));
            }
            else if (anyValue.type() == typeid(bool))
            {
                luaValue = sol::make_object(lua, std::any_cast<bool>(anyValue));
            }
            else if (anyValue.type() == typeid(std::string))
            {
                luaValue = sol::make_object(lua, std::any_cast<std::string>(anyValue));
            }
            else if (anyValue.type() == typeid(glm::vec2))
            {
                luaValue = sol::make_object(lua, std::any_cast<glm::vec2>(anyValue));
            }
            else if (anyValue.type() == typeid(glm::vec3))
            {
                luaValue = sol::make_object(lua, std::any_cast<glm::vec3>(anyValue));
            }
            else if (anyValue.type() == typeid(glm::vec4))
            {
                luaValue = sol::make_object(lua, std::any_cast<glm::vec4>(anyValue));
            }
            else
            {
                YA_CORE_WARN("[LuaScript] Unsupported type for property '{}': {}",
                             propName,
                             anyValue.type().name());
                continue;
            }

            self[propName] = luaValue;
            YA_CORE_TRACE("[LuaScript]   {} = ({})", propName, anyValue.type().name());
        }
        catch (const std::exception &e)
        {
            YA_CORE_ERROR("[LuaScript] Failed to apply property '{}': {}", propName, e.what());
        }
    }
}


} // namespace ya
