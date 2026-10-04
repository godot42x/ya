#pragma once

#include "Core/Api.h"
#include "Core/TypeIndex.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>

namespace ya::script
{

/// A script-held reference to an engine object. It never stores the object's
/// address: `kind` names the provider that can find the object again and
/// `a` / `b` are that provider's own identity for it (a scene instance id plus
/// an entity handle, for example). An object that is gone resolves to nothing
/// instead of dangling.
struct ScriptRef
{
    type_index_t type = 0;
    uint32_t     kind = 0;
    uint64_t     a    = 0;
    uint64_t     b    = 0;

    explicit operator bool() const { return kind != 0; }
    bool     operator==(const ScriptRef&) const = default;
};

/// A Lua function the backend keeps. 0 is empty. Only the Lua backend mints
/// one, from `toScriptValue` of a `LUA_TFUNCTION`.
struct ScriptFnHandle
{
    uint64_t id = 0;
    bool     operator==(const ScriptFnHandle&) const = default;
};

/// One load's `self` table. Pushed back to Lua as that table, not userdata.
/// Compared by `runtimeId` only.
struct ScriptSelfHandle
{
    uint64_t runtimeId = 0;
    bool     operator==(const ScriptSelfHandle&) const = default;
};

/// `{x,y,w,h}` as Lua sees a widget rect. C++ pushes that table. An arbitrary
/// Lua table is not read back as a rect.
struct ScriptRect
{
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    bool  operator==(const ScriptRect&) const = default;
};

/// The only values that cross between engine and script languages. Enums
/// travel as integers; vectors keep their width. The last three arms are
/// Lua-facing; JS rejects them.
using ScriptValue = std::variant<std::monostate,
                                  bool,
                                  int64_t,
                                  double,
                                  std::string,
                                  glm::vec2,
                                  glm::vec3,
                                  glm::vec4,
                                  ScriptRef,
                                  ScriptFnHandle,
                                  ScriptSelfHandle,
                                  ScriptRect>;
using ScriptArgs  = std::span<const ScriptValue>;

/// Anything a script did wrong: unknown member, wrong argument, an object that
/// no longer exists. Backends turn it into their language's error.
struct ScriptError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

[[nodiscard]] YA_CORE_API const char* scriptValueTypeName(const ScriptValue& value);
[[nodiscard]] YA_CORE_API bool        scriptToBool(const ScriptValue& value);
/// Integers, and floats with no fractional part.
[[nodiscard]] YA_CORE_API int64_t     scriptToInteger(const ScriptValue& value);
[[nodiscard]] YA_CORE_API double      scriptToNumber(const ScriptValue& value);
[[nodiscard]] YA_CORE_API std::string scriptToString(const ScriptValue& value);
/// An enum value from its integer or its reflected name.
[[nodiscard]] YA_CORE_API int64_t     scriptToEnum(type_index_t enumType, const ScriptValue& value);

} // namespace ya::script
