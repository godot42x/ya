#include "Script/Lua/LuaScriptingSystem.h"
#include "Core/Log.h"
#include <cmath>
#include <glm/glm.hpp>
#include <limits>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>


namespace ya
{
namespace
{

/// `int` hints narrow to int, then store int64_t. `float` hints narrow to
/// float, then store double. JSON still splits them with `is_number_integer`
/// / `is_number_float`.
script::ScriptValue solToScriptValue(const sol::object& value, std::string_view typeHint)
{
    if (!value.valid() || value.get_type() == sol::type::lua_nil) {
        return {};
    }

    try {
        if (typeHint == "float") {
            return script::scriptMake(static_cast<double>(value.as<float>()));
        }
        if (typeHint == "int") {
            return script::scriptMake(static_cast<int64_t>(value.as<int>()));
        }
        if (typeHint == "bool") {
            return script::scriptMake(value.as<bool>());
        }
        if (typeHint == "string") {
            return script::scriptMake(value.as<std::string>());
        }
        if (typeHint == "Vec3" || typeHint == "vec3") {
            if (value.is<glm::vec3>()) {
                return script::scriptMake(value.as<glm::vec3>());
            }
            if (value.is<sol::table>()) {
                const sol::table table = value.as<sol::table>();
                return script::scriptMake(glm::vec3(table.get_or("x", table.get_or(1, 0.0f)),
                                                    table.get_or("y", table.get_or(2, 0.0f)),
                                                    table.get_or("z", table.get_or(3, 0.0f))));
            }
        }

        switch (value.get_type()) {
        case sol::type::boolean:
            return script::scriptMake(value.as<bool>());
        case sol::type::number:
            return script::scriptMake(static_cast<double>(value.as<float>()));
        case sol::type::string:
            return script::scriptMake(value.as<std::string>());
        default:
            if (value.is<glm::vec3>()) {
                return script::scriptMake(value.as<glm::vec3>());
            }
            return {};
        }
    }
    catch (const sol::error&) {
        return {};
    }
}

/// A Lua value carrying `value`'s type, or an invalid object when the type
/// cannot cross (the caller reports it).
sol::object pushScriptValue(sol::state& lua, const script::ScriptValue& value)
{
    if (value.isNull() || value.isRef() || value.data() == nullptr) {
        return {};
    }
    if (const bool* flag = script::scriptIf<bool>(value)) {
        return sol::make_object(lua, *flag);
    }
    if (const std::string* text = value.asString()) {
        return sol::make_object(lua, *text);
    }
    if (const glm::vec2* vec = script::scriptIf<glm::vec2>(value)) {
        return sol::make_object(lua, *vec);
    }
    if (const glm::vec3* vec = script::scriptIf<glm::vec3>(value)) {
        return sol::make_object(lua, *vec);
    }
    if (const glm::vec4* vec = script::scriptIf<glm::vec4>(value)) {
        return sol::make_object(lua, *vec);
    }
    if (script::scriptIf<script::ScriptRef>(value) || script::scriptIf<script::ScriptFnHandle>(value) ||
        script::scriptIf<script::ScriptSelfHandle>(value)) {
        return {};
    }
    if (refl::TypeRegistry::instance().facet<ya::reflection::FValueStruct>(value.type()) != nullptr) {
        return {};
    }
    const refl::TypeOps* ops = refl::TypeRegistry::instance().find(value.type());
    if (ops != nullptr && ops->kind == refl::EValueKind::Int) {
        return sol::make_object(lua, value.toInt64().value_or(0));
    }
    if (ops != nullptr && ops->kind == refl::EValueKind::Float) {
        return sol::make_object(lua, value.toDouble().value_or(0.0));
    }
    return {};
}

} // namespace

void captureScriptProperties(ScriptInstance& instance, sol::table table)
{
    instance.properties.clear();
    if (!table.valid()) {
        return;
    }

    sol::object propsObject = table["_PROPERTIES"];
    if (!propsObject.valid() || propsObject.get_type() != sol::type::table) {
        return;
    }

    sol::table     propsTable = propsObject.as<sol::table>();
    lua_State* const L        = propsTable.lua_state();
    if (!L) {
        return;
    }

    // sol's range-for iterator keeps lua_next's key on the Lua stack. Any other
    // sol read/write in the loop body unbalances that stack and crashes in
    // luaH_next on the next increment. Snapshot registry refs first, then
    // convert.
    std::vector<std::pair<sol::object, sol::object>> entries;
    propsTable.push();
    const int tableIndex = lua_gettop(L);
    lua_pushnil(L);
    while (lua_next(L, tableIndex) != 0) {
        entries.emplace_back(sol::object(L, -2), sol::object(L, -1));
        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    for (auto& [key, value] : entries) {
        if (!key.is<std::string>() || value.get_type() != sol::type::table) {
            continue;
        }

        const std::string propName = key.as<std::string>();
        sol::table        propDef  = value.as<sol::table>();
        sol::optional<sol::object> propValue = propDef["value"];
        if (!propValue) {
            continue;
        }

        ScriptProperty prop;
        prop.name = propName;

        sol::optional<std::string> typeHint = propDef["type"];
        prop.typeHint = typeHint.value_or("unknown");

        sol::optional<float> minVal = propDef["min"];
        sol::optional<float> maxVal = propDef["max"];
        prop.min                    = minVal.value_or(0.0f);
        prop.max                    = maxVal.value_or(100.0f);

        sol::optional<std::string> tooltip = propDef["tooltip"];
        prop.tooltip                       = tooltip.value_or("");

        sol::object currentValue = table[propName];
        if (!currentValue.valid() || currentValue.get_type() == sol::type::lua_nil) {
            table[propName] = *propValue;
            currentValue    = *propValue;
        }
        prop.value = solToScriptValue(currentValue, prop.typeHint);
        instance.properties.push_back(std::move(prop));
    }
}

void applyScriptPropertyOverrides(const ScriptInstance& instance, sol::table table, sol::state& lua)
{
    if (!table.valid() || instance.propertyOverrides.empty()) {
        return;
    }

    YA_CORE_INFO("[LuaScript] Applying {} property overrides to {}",
                 instance.propertyOverrides.size(),
                 instance.scriptPath);

    for (const auto& [propName, value] : instance.propertyOverrides) {
        if ((value).isNull()) {
            YA_CORE_WARN("[LuaScript] Property '{}' has no value, skipping", propName);
            continue;
        }

        try {
            const sol::object luaValue = pushScriptValue(lua, value);
            if (!luaValue.valid()) {
                YA_CORE_WARN("[LuaScript] Unsupported type for property '{}': {}",
                             propName,
                             script::scriptValueTypeName(value));
                continue;
            }

            table[propName] = luaValue;
            YA_CORE_TRACE("[LuaScript]   {} = ({})", propName, script::scriptValueTypeName(value));
        }
        catch (const std::exception& e) {
            YA_CORE_ERROR("[LuaScript] Failed to apply property '{}': {}", propName, e.what());
        }
    }
}


} // namespace ya

