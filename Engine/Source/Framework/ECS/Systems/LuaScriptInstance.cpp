#include "ECS/Systems/LuaScriptInstance.h"
#include "Core/Log.h"
#include "Core/System/PathUtils.h"
#include "Resource/AssetManager.h"
#include <any>
#include <cmath>
#include <glm/glm.hpp>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>


namespace ya
{
namespace
{

std::any solToAny(const sol::object& value, std::string_view typeHint)
{
    if (!value.valid() || value.get_type() == sol::type::lua_nil) {
        return {};
    }

    try {
        if (typeHint == "float") {
            return value.as<float>();
        }
        if (typeHint == "int") {
            return value.as<int>();
        }
        if (typeHint == "bool") {
            return value.as<bool>();
        }
        if (typeHint == "string") {
            return value.as<std::string>();
        }
        if (typeHint == "Vec3" || typeHint == "vec3") {
            if (value.is<glm::vec3>()) {
                return value.as<glm::vec3>();
            }
            if (value.is<sol::table>()) {
                const sol::table table = value.as<sol::table>();
                return glm::vec3(table.get_or("x", table.get_or(1, 0.0f)),
                                 table.get_or("y", table.get_or(2, 0.0f)),
                                 table.get_or("z", table.get_or(3, 0.0f)));
            }
        }

        switch (value.get_type()) {
        case sol::type::boolean:
            return value.as<bool>();
        case sol::type::number:
            return value.as<float>();
        case sol::type::string:
            return value.as<std::string>();
        default:
            if (value.is<glm::vec3>()) {
                return value.as<glm::vec3>();
            }
            return {};
        }
    }
    catch (const sol::error&) {
        return {};
    }
}

/// A Lua value carrying `value`'s type, or an invalid object when the type
/// cannot cross (the caller reports it). The type list is the whole dispatch.
template <typename... TTypes>
sol::object makeLuaObject(sol::state& lua, const std::any& value)
{
    sol::object made;
    const bool  bKnown = ((value.type() == typeid(TTypes)
                               ? (made = sol::make_object(lua, std::any_cast<TTypes>(value)), true)
                               : false)
                              || ...);
    return bKnown ? made : sol::object{};
}

} // namespace

std::string LuaScriptInstance::normalizeScriptPath(std::string_view path)
{
    return AssetManager::normalizeScriptAssetPath(path);
}

void LuaScriptInstance::refreshProperties()
{
    if (!self.valid()) {
        properties.clear();
        return;
    }
    capturePropertiesFrom(self);
}

void LuaScriptInstance::capturePropertiesFrom(sol::table table)
{
    properties.clear();
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

        LuaScriptProperty prop;
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
        prop.value = solToAny(currentValue, prop.typeHint);
        properties.push_back(std::move(prop));
    }
}

void LuaScriptInstance::releaseLuaHandles()
{
    listeners.reset();
    self      = sol::lua_nil;
    onInit    = sol::lua_nil;
    onStart   = sol::lua_nil;
    onUpdate  = sol::lua_nil;
    onDestroy = sol::lua_nil;
    onEnable  = sol::lua_nil;
    onDisable = sol::lua_nil;
}

void LuaScriptInstance::applyPropertyOverrides(sol::state& lua)
{
    applyPropertyOverridesTo(self, lua);
}

void LuaScriptInstance::applyPropertyOverridesTo(sol::table table, sol::state& lua)
{
    if (!table.valid() || propertyOverrides.empty()) {
        return;
    }

    YA_CORE_INFO("[LuaScript] Applying {} property overrides to {}",
                 propertyOverrides.size(),
                 scriptPath);

    for (const auto& [propName, anyValue] : propertyOverrides) {
        if (!anyValue.has_value()) {
            YA_CORE_WARN("[LuaScript] Property '{}' has no value, skipping", propName);
            continue;
        }

        try {
            const sol::object luaValue =
                makeLuaObject<int, float, double, bool, std::string, glm::vec2, glm::vec3, glm::vec4>(lua, anyValue);
            if (!luaValue.valid()) {
                YA_CORE_WARN("[LuaScript] Unsupported type for property '{}': {}",
                             propName,
                             anyValue.type().name());
                continue;
            }

            table[propName] = luaValue;
            YA_CORE_TRACE("[LuaScript]   {} = ({})", propName, anyValue.type().name());
        }
        catch (const std::exception& e) {
            YA_CORE_ERROR("[LuaScript] Failed to apply property '{}': {}", propName, e.what());
        }
    }
}


} // namespace ya

