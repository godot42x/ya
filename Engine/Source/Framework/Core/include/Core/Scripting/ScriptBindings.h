#pragma once

#include "Core/Scripting/ScriptValue.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>

/// The one reflection -> script export shared by every script language.
///
/// A type shows scripts every reflected, non-static member whose value can
/// cross (see ScriptValue), under its reflected name without leading
/// underscores; fields that are const or have no mutable accessor are
/// read-only. Members are found through the reflection plugin's own
/// Class / Property / Function and methods run through `Function::invoker`.
/// This layer adds only what the plugin has no notion of: script values,
/// references that survive their object, native methods a module supplies
/// for things that are not reflected members, and module functions that
/// belong to no object (`world.find`). How a value or an object looks in Lua
/// or JS is the backend's business.
///
/// Gameplay scripts call through this layer every frame with live objects.
/// Authoring and automation commands with structured JSON arguments and
/// results (`component.get`, `scene.save`...) live in ScriptApiRegistry.
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

/// A function a module offers scripts as `<module>.<name>(...)`.
using ScriptFunction = std::function<ScriptValue(ScriptArgs args)>;

struct ScriptModuleFunction
{
    std::string    module;
    std::string    name;
    ScriptFunction fn;
};

/// A member found by name once. Handles stay valid for the whole process, so
/// backends may cache them per type; a method a resolver answered keeps its
/// first answer.
struct ScriptField;
struct ScriptMethod;

/// Returns the kind id put into references of this provider (never 0).
YA_CORE_API uint32_t registerRefKind(ScriptRefKind kind);
/// A reflected method of the same name wins over a native one.
YA_CORE_API void     addNativeMethod(type_index_t type, std::string name, ScriptNativeFn fn);
YA_CORE_API void     setMethodResolver(type_index_t type, ScriptMethodResolver resolver);

/// Backends project module functions when they build a script state, so a
/// module registers before the states it serves are created. Registering
/// the same module and name again replaces the function.
YA_CORE_API void registerModuleFunction(std::string module, std::string name, ScriptFunction fn);
/// In registration order. Entries never move, so backends may keep pointers.
YA_CORE_API void forEachModuleFunction(const std::function<void(const ScriptModuleFunction&)>& visit);

/// The reflected class name, or empty for a type without one.
[[nodiscard]] YA_CORE_API std::string         scriptTypeName(type_index_t type);
[[nodiscard]] YA_CORE_API const ScriptField*  findField(type_index_t type, std::string_view name);
[[nodiscard]] YA_CORE_API const ScriptMethod* findMethod(type_index_t type, std::string_view name);

/// Null when the object is gone or the reference is empty.
[[nodiscard]] YA_CORE_API void* tryResolve(const ScriptRef& ref);
/// Throws ScriptError when the object is gone.
[[nodiscard]] YA_CORE_API void* resolve(const ScriptRef& ref);

/// `ref` must be of the type the handle was found on.
[[nodiscard]] YA_CORE_API ScriptValue readField(const ScriptRef& ref, const ScriptField& field);
YA_CORE_API void                      writeField(const ScriptRef& ref, const ScriptField& field, const ScriptValue& value);
YA_CORE_API ScriptValue               callMethod(const ScriptRef& ref, const ScriptMethod& method, ScriptArgs args);

/// Find-then-use in one call; an unknown name throws ScriptError.
[[nodiscard]] YA_CORE_API ScriptValue readField(const ScriptRef& ref, std::string_view field);
YA_CORE_API void                      writeField(const ScriptRef& ref, std::string_view field, const ScriptValue& value);
YA_CORE_API ScriptValue               callMethod(const ScriptRef& ref, std::string_view method, ScriptArgs args);

} // namespace ya::script
