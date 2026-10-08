#pragma once

#include "Core/Scripting/ScriptValue.h"

#include <reflects-core/lib.h>

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

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
/// A native method registered before export keeps its name: at export,
/// reflected methods do not overwrite it. After export, the first
/// registration of a name wins; a later one neither replaces the function
/// nor patches its signature.
/// `argTypes` is the script-facing signature. Empty means every argument must
/// convert on its own. A `ScriptSelfHandle` parameter is an owner/self slot.
/// Prefer `addMethod` + `fn` for new registrations. This entry stays until the
/// remaining call sites move.
YA_CORE_API void     addNativeMethod(type_index_t type,
                                      std::string name,
                                      ScriptNativeFn fn,
                                      std::vector<type_index_t> argTypes = {});

/// No `ScriptSelfHandle` parameter.
inline constexpr std::size_t kNoOwnerSlot = static_cast<std::size_t>(-1);

/// Types a script argument may be. The compile-time check follows `ScriptValue`'s
/// variant arms, plus arithmetic types and enums (`scriptToEnum`). `ScriptValue`
/// itself is not an arm, so it cannot be a parameter; a `ScriptValue` return is
/// allowed separately. `ScriptSelfHandle` is the owner slot and still counts:
/// the adapter pulls it out of `ScriptArgs`.
template <typename T>
constexpr bool crossesToScripts();

/// Runtime check: a value codec or a reflected enum is registered for `type`.
[[nodiscard]] YA_CORE_API bool crossesToScripts(type_index_t type);

template <typename... Params>
constexpr std::size_t nativeOwnerCount();

/// Index of the `ScriptSelfHandle` parameter among script arguments (object
/// receivers are not script arguments), or `kNoOwnerSlot`.
template <typename... Params>
constexpr std::size_t nativeOwnerSlot();

/// True when `fn` can bind `Ret(Params...)`: at most one object receiver, at
/// most one `ScriptSelfHandle`, and every other parameter crosses.
template <typename Ret, typename... Params>
constexpr bool nativeSignatureOk();

/// Typed native method. `args` is the script-facing signature (no object
/// receiver). `invoker` is the MetaValue `Function` path installed by addMethod.
struct NativeMethod
{
    std::vector<refl::ParamDesc>                                         args;
    std::vector<std::string>                                             argTypeNames;
    refl::ParamDesc                                                      ret;
    std::string                                                          returnTypeName;
    std::size_t                                                          ownerSlot = kNoOwnerSlot;
    bool                                                                 bConst = false;
    std::function<refl::MetaValue(void*, std::span<refl::MetaValue>)>    invoker;
};

/// Deduce a free function. A reference or pointer to a class that does not
/// cross is the C++ object (`Entity&` from `void* self`). A `ScriptSelfHandle`
/// parameter is the owner slot. Every other parameter is converted from
/// `ScriptValue` before the body runs.
template <typename Ret, typename... Params>
[[nodiscard]] NativeMethod fn(Ret (*function)(Params...));

/// Deduce a lambda or functor with one `operator()`.
template <typename F>
    requires requires { &std::remove_cvref_t<F>::operator(); }
[[nodiscard]] NativeMethod fn(F&& functor);

/// Inject `method` into `ClassRegistry::functions` (first name wins; a later
/// reflected registration does not replace it). The method is an ordinary
/// Function whose invoker speaks MetaValue. Must run before the type's first export.
YA_CORE_API void addMethod(type_index_t type, std::string name, NativeMethod method);
/// A native field hides a reflected field of the same name. The existing
/// entry is updated in place, so a handle already handed to a backend stays
/// valid. A second native registration of `name` does not replace the first.
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
/// Declared parameter type at `index`, or 0 when the method does not declare
/// one (native method registered without a signature, or past the last
/// parameter). Reflected methods report their reflected signature.
[[nodiscard]] YA_CORE_API type_index_t        methodArgumentType(const ScriptMethod& method, size_t index);

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

namespace detail
{

template <typename T, typename Variant>
struct is_variant_alternative : std::false_type
{
};

template <typename T, typename... Alts>
struct is_variant_alternative<T, std::variant<Alts...>> : std::bool_constant<(std::is_same_v<T, Alts> || ...)>
{
};

template <typename T, typename Variant>
inline constexpr bool is_variant_alternative_v = is_variant_alternative<T, Variant>::value;

} // namespace detail

template <typename T>
constexpr bool crossesToScripts()
{
    using D = std::decay_t<T>;
    // Arms of ScriptValue, plus arithmetic types and enums (scriptToEnum).
    // ScriptValue is not an arm of itself, so it cannot be a parameter.
    return detail::is_variant_alternative_v<D, ScriptValue> || std::is_arithmetic_v<D> || std::is_enum_v<D>;
}

} // namespace ya::script

#include "Core/Scripting/ScriptBindingsDetail.h"

namespace ya::script
{

template <typename... Params>
constexpr std::size_t nativeOwnerCount()
{
    return (std::size_t{detail::isOwnerParameter<Params>()} + ... + std::size_t{0});
}

template <typename... Params>
constexpr std::size_t nativeOwnerSlot()
{
    if constexpr (sizeof...(Params) == 0) {
        return kNoOwnerSlot;
    }
    else {
        return detail::ownerSlotFrom<Params...>(0);
    }
}

template <typename Ret, typename... Params>
constexpr bool nativeSignatureOk()
{
    constexpr std::size_t owners    = nativeOwnerCount<Params...>();
    constexpr std::size_t receivers = (std::size_t{detail::isObjectReceiver<Params>()} + ... + std::size_t{0});
    if (owners > 1 || receivers > 1) {
        return false;
    }
    if (!detail::returnOk<Ret>()) {
        return false;
    }
    return (detail::parameterOk<Params>() && ... && true);
}

template <typename Ret, typename... Params>
NativeMethod fn(Ret (*function)(Params...))
{
    return detail::bindNative<Ret, Params...>(function);
}

template <typename F>
    requires requires { &std::remove_cvref_t<F>::operator(); }
NativeMethod fn(F&& functor)
{
    return detail::bindFunctor(std::forward<F>(functor), &std::remove_cvref_t<F>::operator());
}

} // namespace ya::script
