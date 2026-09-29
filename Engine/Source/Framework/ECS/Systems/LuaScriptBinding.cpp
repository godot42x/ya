#include "ECS/Systems/LuaScriptBinding.h"

#include "Core/Scripting/ScriptBindings.h"
#include "Scene/Core/SceneScriptBindings.h"

#include <glm/glm.hpp>

#include <format>
#include <vector>

namespace ya
{

namespace
{

using script::ScriptError;
using script::ScriptValue;

std::string typeNameOf(const script::ScriptRef& ref)
{
    std::string name = script::scriptTypeName(ref.type);
    return name.empty() ? std::string("object") : name;
}

/// Calls the method named by upvalue 1 on argument 1. Pushes the result, or
/// the error message and returns -1; no C++ object may be alive when the
/// caller raises the Lua error.
int callMethod(lua_State* L)
{
    try {
        const auto self = sol::stack::check_get<LuaScriptObject>(L, 1);
        const char* name = lua_tostring(L, lua_upvalueindex(1));
        if (!self) {
            throw ScriptError(std::format("call '{}' with ':' on an engine object", name));
        }
        std::vector<ScriptValue> args;
        const int                top = lua_gettop(L);
        args.reserve(top > 1 ? top - 1 : 0);
        for (int index = 2; index <= top; ++index) {
            args.push_back(fromLuaValue(sol::object(L, index)));
        }
        const ScriptValue result = script::callMethod(self->ref, name, args);
        return toLuaValue(L, result).push(L);
    }
    catch (const std::exception& e) {
        lua_pushstring(L, e.what());
        return -1;
    }
}

int methodTrampoline(lua_State* L)
{
    const int results = callMethod(L);
    return results >= 0 ? results : lua_error(L);
}

sol::object scriptIndex(sol::this_state state, const LuaScriptObject& self, const std::string& key)
{
    switch (script::findMember(self.ref.type, key)) {
    case script::EScriptMember::Field:
        return toLuaValue(state, script::readField(self.ref, key));
    case script::EScriptMember::Method: {
        lua_State* L = state;
        lua_pushlstring(L, key.data(), key.size());
        lua_pushcclosure(L, &methodTrampoline, 1);
        return sol::stack::pop<sol::object>(L);
    }
    case script::EScriptMember::None:
        break;
    }
    throw ScriptError(std::format("{} has no field or method '{}'", typeNameOf(self.ref), key));
}

void scriptNewIndex(const LuaScriptObject& self, const std::string& key, const sol::object& value)
{
    script::writeField(self.ref, key, fromLuaValue(value));
}

} // namespace

void registerLuaScriptBindings(sol::state_view lua)
{
    script::ensureSceneScriptBindings();

    lua.new_usertype<glm::vec2>("Vec2",
                                sol::constructors<glm::vec2(), glm::vec2(float), glm::vec2(float, float)>(),
                                "x",
                                &glm::vec2::x,
                                "y",
                                &glm::vec2::y,
                                "__add",
                                [](const glm::vec2& a, const glm::vec2& b) { return a + b; },
                                "__sub",
                                [](const glm::vec2& a, const glm::vec2& b) { return a - b; },
                                "__mul",
                                sol::overload([](const glm::vec2& v, float s) { return v * s; },
                                              [](float s, const glm::vec2& v) { return s * v; }),
                                "__div",
                                [](const glm::vec2& v, float s) { return v / s; },
                                "length",
                                [](const glm::vec2& v) { return glm::length(v); },
                                "normalize",
                                [](const glm::vec2& v) { return glm::normalize(v); });

    lua.new_usertype<glm::vec3>("Vec3",
                                sol::constructors<glm::vec3(), glm::vec3(float), glm::vec3(float, float, float)>(),
                                "x",
                                &glm::vec3::x,
                                "y",
                                &glm::vec3::y,
                                "z",
                                &glm::vec3::z,
                                "__add",
                                [](const glm::vec3& a, const glm::vec3& b) { return a + b; },
                                "__sub",
                                [](const glm::vec3& a, const glm::vec3& b) { return a - b; },
                                "__mul",
                                sol::overload([](const glm::vec3& v, float s) { return v * s; },
                                              [](float s, const glm::vec3& v) { return s * v; }),
                                "__div",
                                [](const glm::vec3& v, float s) { return v / s; },
                                "length",
                                [](const glm::vec3& v) { return glm::length(v); },
                                "normalize",
                                [](const glm::vec3& v) { return glm::normalize(v); },
                                "dot",
                                [](const glm::vec3& a, const glm::vec3& b) { return glm::dot(a, b); },
                                "cross",
                                [](const glm::vec3& a, const glm::vec3& b) { return glm::cross(a, b); });

    lua.new_usertype<glm::vec4>("Vec4",
                                sol::constructors<glm::vec4(), glm::vec4(float), glm::vec4(float, float, float, float)>(),
                                "x",
                                &glm::vec4::x,
                                "y",
                                &glm::vec4::y,
                                "z",
                                &glm::vec4::z,
                                "w",
                                &glm::vec4::w);

    lua.new_usertype<LuaScriptObject>(
        "ScriptObject",
        sol::no_constructor,
        sol::meta_function::index,
        &scriptIndex,
        sol::meta_function::new_index,
        &scriptNewIndex,
        sol::meta_function::equal_to,
        [](const LuaScriptObject& a, const LuaScriptObject& b) { return a.ref == b.ref; },
        sol::meta_function::to_string,
        [](const LuaScriptObject& self) { return typeNameOf(self.ref); });
}

sol::object toLuaValue(sol::state_view lua, const ScriptValue& value)
{
    return std::visit(
        [&](const auto& v) -> sol::object {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return sol::make_object(lua, sol::lua_nil);
            }
            else if constexpr (std::is_same_v<T, script::ScriptRef>) {
                return v ? sol::make_object(lua, LuaScriptObject{v}) : sol::make_object(lua, sol::lua_nil);
            }
            else {
                return sol::make_object(lua, v);
            }
        },
        value);
}

ScriptValue fromLuaValue(const sol::object& value)
{
    switch (value.get_type()) {
    case sol::type::lua_nil:
    case sol::type::none:
        return {};
    case sol::type::boolean:
        return value.as<bool>();
    case sol::type::number: {
        lua_State* L = value.lua_state();
        value.push(L);
        const bool        bInteger = lua_isinteger(L, -1) != 0;
        const lua_Integer integer  = bInteger ? lua_tointeger(L, -1) : 0;
        const lua_Number  number   = lua_tonumber(L, -1);
        lua_pop(L, 1);
        return bInteger ? ScriptValue{static_cast<int64_t>(integer)} : ScriptValue{static_cast<double>(number)};
    }
    case sol::type::string:
        return value.as<std::string>();
    case sol::type::userdata:
        if (value.is<glm::vec3>()) {
            return value.as<glm::vec3>();
        }
        if (value.is<glm::vec2>()) {
            return value.as<glm::vec2>();
        }
        if (value.is<glm::vec4>()) {
            return value.as<glm::vec4>();
        }
        if (value.is<LuaScriptObject>()) {
            return value.as<LuaScriptObject>().ref;
        }
        break;
    default:
        break;
    }
    throw ScriptError(std::format("a Lua {} cannot be passed to the engine", sol::type_name(value.lua_state(), value.get_type())));
}

} // namespace ya
