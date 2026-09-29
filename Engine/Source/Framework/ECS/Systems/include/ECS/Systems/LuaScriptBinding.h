#pragma once

#include "Core/Scripting/ScriptValue.h"

#include <sol/sol.hpp>

namespace ya
{

/// An engine object as Lua holds it: only its script reference. Fields and
/// methods are looked up by name on every access through the shared script
/// export, so any reflected type marked `.script()` works without a binding
/// of its own, and an object that is gone raises a Lua error instead of
/// touching freed memory.
struct LuaScriptObject
{
    script::ScriptRef ref;
};

/// `Vec2` / `Vec3` / `Vec4` and the script object type. Called once per state.
YA_ECS_SYSTEMS_API void registerLuaScriptBindings(sol::state_view lua);

[[nodiscard]] YA_ECS_SYSTEMS_API sol::object         toLuaValue(sol::state_view lua, const script::ScriptValue& value);
/// Throws script::ScriptError for a value no engine type can take.
[[nodiscard]] YA_ECS_SYSTEMS_API script::ScriptValue fromLuaValue(const sol::object& value);

} // namespace ya
