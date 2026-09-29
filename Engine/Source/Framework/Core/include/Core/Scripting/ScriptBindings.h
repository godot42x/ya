#pragma once

#include "Core/Scripting/ScriptValue.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>

/// The one reflection -> script export shared by every script language.
///
/// What a type shows scripts is decided by its reflection marks alone:
/// `.script()` / `.scriptReadOnly()` fields and `.script()` methods, found
/// through the reflection plugin's own Class / Property / Function and invoked
/// through `Function::invoker`. This layer adds only what the plugin has no
/// notion of: script values, references that survive their object, and
/// native methods a module supplies for things that are not reflected
/// members. How a value or an object looks in Lua or JS is the backend's
/// business.
namespace ya::script
{

/// A method a module supplies in code (entity component access, for one).
/// `self` is the resolved object.
using ScriptNativeFn = std::function<ScriptValue(void* self, const ScriptRef& selfRef, ScriptArgs args)>;

/// Answers method names a type cannot list up front (per-component accessors
/// on entities depend on which components are registered at call time).
using ScriptMethodResolver = std::function<std::optional<ScriptNativeFn>(std::string_view methodName)>;

/// A provider of script references: finds the object again from the ids it
/// put into the reference, or returns null when the object is gone.
struct ScriptRefKind
{
    std::string                                  name;
    std::function<void*(const ScriptRef& ref)>   resolve;
    /// Runs after a script wrote a field of the resolved object.
    std::function<void(const ScriptRef&, void*)> afterWrite;
};

enum class EScriptMember : uint8_t
{
    None,
    Field,
    Method,
};

/// Returns the kind id put into references of this provider (never 0).
YA_CORE_API uint32_t registerRefKind(ScriptRefKind kind);
YA_CORE_API void     addNativeMethod(type_index_t type, std::string name, ScriptNativeFn fn);
YA_CORE_API void     setMethodResolver(type_index_t type, ScriptMethodResolver resolver);

/// The reflected class name, or empty for a type without one.
[[nodiscard]] YA_CORE_API std::string   scriptTypeName(type_index_t type);
[[nodiscard]] YA_CORE_API EScriptMember findMember(type_index_t type, std::string_view name);

/// Null when the object is gone or the reference is empty.
[[nodiscard]] YA_CORE_API void* tryResolve(const ScriptRef& ref);
/// Throws ScriptError when the object is gone.
[[nodiscard]] YA_CORE_API void* resolve(const ScriptRef& ref);

[[nodiscard]] YA_CORE_API ScriptValue readField(const ScriptRef& ref, std::string_view field);
YA_CORE_API void                      writeField(const ScriptRef& ref, std::string_view field, const ScriptValue& value);
YA_CORE_API ScriptValue               callMethod(const ScriptRef& ref, std::string_view method, ScriptArgs args);

} // namespace ya::script
