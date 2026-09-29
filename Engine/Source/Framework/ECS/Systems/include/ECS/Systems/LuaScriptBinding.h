#pragma once

#include "Core/Scripting/ScriptValue.h"

#include <sol/sol.hpp>

namespace ya
{

/// An engine object as Lua holds it: only its script reference, as a full
/// userdata whose metatable belongs to the object's type. The first access
/// of a member name on a type looks it up in the shared script export; the
/// answer (a method closure or a field handle) is cached in that type's
/// metatable, so later accesses on any object of the type cost one table
/// lookup. An object that is gone still raises a Lua error instead of
/// touching freed memory, because the reference is resolved on every use.
struct LuaScriptObject
{
    script::ScriptRef ref;
};

/// `Vec2` / `Vec3` / `Vec4`. Called once per state; script objects need no
/// registration. The full mechanism of script objects in Lua is documented
/// at the top of LuaScriptBinding.cpp.
YA_ECS_SYSTEMS_API void registerLuaScriptBindings(sol::state_view lua);

YA_ECS_SYSTEMS_API int                 pushLuaValue(lua_State* L, const script::ScriptValue& value);
/// Throws script::ScriptError for a value no engine type can take.
[[nodiscard]] YA_ECS_SYSTEMS_API script::ScriptValue toScriptValue(lua_State* L, int index);

[[nodiscard]] YA_ECS_SYSTEMS_API bool isLuaScriptObject(lua_State* L, int index);

// sol2 customization: LuaScriptObject crosses as the userdata above.
YA_ECS_SYSTEMS_API int             sol_lua_push(lua_State* L, const LuaScriptObject& object);
YA_ECS_SYSTEMS_API LuaScriptObject sol_lua_get(sol::types<LuaScriptObject>, lua_State* L, int index, sol::stack::record& tracking);

template <typename Handler>
bool sol_lua_check(sol::types<LuaScriptObject>, lua_State* L, int index, Handler&& handler, sol::stack::record& tracking)
{
    tracking.use(1);
    if (isLuaScriptObject(L, index)) {
        return true;
    }
    handler(L, index, sol::type::userdata, sol::type_of(L, index), "expected an engine object");
    return false;
}

} // namespace ya
