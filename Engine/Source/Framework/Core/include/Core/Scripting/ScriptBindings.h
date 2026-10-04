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
    /// Runs after a script called a non-const method on the resolved object.
    /// Const methods and native methods do not reach it.
    std::function<void(const ScriptRef&, void*)> afterCall;
    /// True: a missing object reads as nil and ignores writes and calls,
    /// instead of throwing ScriptError. Entity kinds leave this false.
    bool missingIsNil = false;
};

/// A field a module supplies in code. `property == nullptr` on the found
/// field means these run. An empty `write` is read-only.
struct ScriptNativeField
{
    std::function<ScriptValue(void* self, const ScriptRef&)>                         read;
    std::function<void(void* self, const ScriptRef&, const ScriptValue&)>            write;
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
/// A reflected field of the same name wins. A second registration of `name`
/// does not replace the first.
YA_CORE_API void     addNativeField(type_index_t type, std::string name, ScriptNativeField field);
/// `derived`'s lookup continues into `base` when its own table misses.
/// The first base wins; a later call does not replace it. A member registered
/// on `derived` hides the same name on `base`.
YA_CORE_API void     addScriptBase(type_index_t derived, type_index_t base);
/// `scriptTypeName` for a type with no reflected class, or an override of one.
YA_CORE_API void     setScriptTypeName(type_index_t type, std::string name);
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

/// Language backend behind `ScriptFnHandle`. GameRuntime calls through this
/// and does not see the language runtime. Null until the backend installs.
struct IScriptCallbacks
{
    virtual ~IScriptCallbacks() = default;

    virtual void retain(ScriptFnHandle fn)                                               = 0;
    virtual void release(ScriptFnHandle fn)                                              = 0;
    /// Call `fn` with `self` first when `self.runtimeId != 0`, then `args`.
    /// Errors are logged. This does not throw into the language state.
    virtual void invoke(ScriptFnHandle fn, ScriptSelfHandle self, ScriptArgs args) = 0;
};

YA_CORE_API void              setScriptCallbacks(IScriptCallbacks* callbacks);
YA_CORE_API IScriptCallbacks* scriptCallbacks();

} // namespace ya::script
