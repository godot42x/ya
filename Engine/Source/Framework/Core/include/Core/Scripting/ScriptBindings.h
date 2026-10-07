#pragma once

#include "Core/Scripting/ScriptValue.h"

#include <reflects-core/lib.h>

#include <any>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <utility>
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

/// Types a script argument may be. Enums travel as integers. `ScriptSelfHandle`
/// is the owner slot and still counts: the adapter pulls it out of `ScriptArgs`.
template <typename T>
constexpr bool crossesToScripts();

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

/// Typed native method. `argTypes` is the script-facing signature (no object
/// receiver). `call` is the ScriptValue trampoline; `invoker` is the boxed
/// `Function` path.
struct NativeMethod
{
    std::vector<type_index_t>                              argTypes;
    std::vector<std::string>                               argTypeNames;
    type_index_t                                           returnType = 0;
    std::string                                            returnTypeName;
    std::size_t                                            ownerSlot = kNoOwnerSlot;
    ScriptNativeFn                                         call;
    std::function<std::any(void*, const ArgumentList&)>    invoker;
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
/// reflected registration does not replace it) and keep `method.call` as the
/// script trampoline. Must run before the type's first export.
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

template <typename T>
constexpr bool isOwnerParameter()
{
    return std::is_same_v<std::decay_t<T>, ScriptSelfHandle>;
}

template <typename T>
constexpr bool isObjectReceiver()
{
    using NoRef             = std::remove_reference_t<T>;
    constexpr bool bPointer = std::is_pointer_v<NoRef>;
    if constexpr (!bPointer && !std::is_lvalue_reference_v<T>) {
        return false;
    }
    else {
        using Pointee = std::remove_cv_t<std::conditional_t<bPointer, std::remove_pointer_t<NoRef>, NoRef>>;
        if constexpr (!std::is_class_v<Pointee>) {
            return false;
        }
        else {
            return !crossesToScripts<Pointee>();
        }
    }
}

template <typename T>
constexpr bool parameterOk()
{
    if constexpr (isObjectReceiver<T>()) {
        return true;
    }
    else if constexpr (std::is_lvalue_reference_v<T> && !std::is_const_v<std::remove_reference_t<T>>) {
        return false;
    }
    else {
        return crossesToScripts<std::decay_t<T>>();
    }
}

template <typename Ret>
constexpr bool returnOk()
{
    using D = std::decay_t<Ret>;
    return std::is_void_v<D> || std::is_same_v<D, ScriptValue> || crossesToScripts<D>();
}

template <typename Head, typename... Tail>
constexpr std::size_t ownerSlotFrom(std::size_t index)
{
    if constexpr (isObjectReceiver<Head>()) {
        if constexpr (sizeof...(Tail) == 0) {
            return kNoOwnerSlot;
        }
        else {
            return ownerSlotFrom<Tail...>(index);
        }
    }
    else if constexpr (isOwnerParameter<Head>()) {
        return index;
    }
    else if constexpr (sizeof...(Tail) == 0) {
        return kNoOwnerSlot;
    }
    else {
        return ownerSlotFrom<Tail...>(index + 1);
    }
}

template <typename T>
T fromScript(const ScriptValue& value)
{
    if constexpr (std::is_same_v<T, bool>) {
        return scriptToBool(value);
    }
    else if constexpr (std::is_integral_v<T>) {
        return static_cast<T>(scriptToInteger(value));
    }
    else if constexpr (std::is_floating_point_v<T>) {
        return static_cast<T>(scriptToNumber(value));
    }
    else if constexpr (std::is_same_v<T, std::string>) {
        return scriptToString(value);
    }
    else if constexpr (std::is_same_v<T, ScriptRef>) {
        if (const ScriptRef* ref = std::get_if<ScriptRef>(&value)) {
            return *ref;
        }
        if (std::holds_alternative<std::monostate>(value)) {
            return {};
        }
        throw ScriptError(std::string("expected an object, got ") + scriptValueTypeName(value));
    }
    else if constexpr (std::is_enum_v<T>) {
        return static_cast<T>(scriptToEnum(type_index_v<T>, value));
    }
    else if constexpr (std::is_same_v<T, ScriptSelfHandle> || std::is_same_v<T, ScriptFnHandle> || std::is_same_v<T, ScriptRect>) {
        if (const T* held = std::get_if<T>(&value)) {
            return *held;
        }
        throw ScriptError(std::string("expected ") + scriptValueTypeName(T{}) + ", got " + scriptValueTypeName(value));
    }
    else {
        if (const T* held = std::get_if<T>(&value)) {
            return *held;
        }
        throw ScriptError(std::string("expected a vector, got ") + scriptValueTypeName(value));
    }
}

template <typename T>
ScriptValue toScript(const T& value)
{
    if constexpr (std::is_same_v<T, bool>) {
        return value;
    }
    else if constexpr (std::is_integral_v<T>) {
        return static_cast<int64_t>(value);
    }
    else if constexpr (std::is_floating_point_v<T>) {
        return static_cast<double>(value);
    }
    else if constexpr (std::is_same_v<T, ScriptRef>) {
        return value ? ScriptValue{value} : ScriptValue{};
    }
    else if constexpr (std::is_enum_v<T>) {
        return static_cast<int64_t>(value);
    }
    else {
        return value;
    }
}

template <typename Param>
using Stored = std::conditional_t<isObjectReceiver<Param>(), Param, std::decay_t<Param>>;

template <typename Param>
decltype(auto) bindReceiver(void* self)
{
    using NoRef = std::remove_reference_t<Param>;
    if constexpr (std::is_pointer_v<NoRef>) {
        using Pointee = std::remove_pointer_t<NoRef>;
        return static_cast<NoRef>(static_cast<std::remove_cv_t<Pointee>*>(self));
    }
    else {
        using Obj = std::remove_cv_t<NoRef>;
        return static_cast<NoRef&>(*static_cast<Obj*>(self));
    }
}

template <typename Param>
decltype(auto) takeScript(void* self, ScriptArgs args, std::size_t& cursor)
{
    if constexpr (isObjectReceiver<Param>()) {
        return bindReceiver<Param>(self);
    }
    else {
        const ScriptValue& value = args[cursor++];
        return fromScript<std::decay_t<Param>>(value);
    }
}

template <typename Param>
decltype(auto) takeAny(void* self, const ArgumentList& args, std::size_t& cursor)
{
    if constexpr (isObjectReceiver<Param>()) {
        return bindReceiver<Param>(self);
    }
    else {
        const std::size_t index = cursor++;
        return args.get<std::decay_t<Param>>(index);
    }
}

template <typename Ret, typename Fn, typename Tuple>
ScriptValue applyScript(Fn& body, Tuple&& bound)
{
    return std::apply(
        [&](auto&&... params) -> ScriptValue {
            if constexpr (std::is_void_v<Ret>) {
                body(std::forward<decltype(params)>(params)...);
                return {};
            }
            else if constexpr (std::is_same_v<std::decay_t<Ret>, ScriptValue>) {
                return body(std::forward<decltype(params)>(params)...);
            }
            else {
                return toScript(body(std::forward<decltype(params)>(params)...));
            }
        },
        std::forward<Tuple>(bound));
}

template <typename Ret, typename Fn, typename Tuple>
std::any applyAny(Fn& body, Tuple&& bound)
{
    if constexpr (std::is_void_v<Ret>) {
        std::apply([&](auto&&... params) { body(std::forward<decltype(params)>(params)...); }, std::forward<Tuple>(bound));
        return {};
    }
    else {
        return std::any(std::apply(
            [&](auto&&... params) -> std::decay_t<Ret> { return body(std::forward<decltype(params)>(params)...); },
            std::forward<Tuple>(bound)));
    }
}

template <typename Ret, typename... Params, typename Fn>
ScriptValue invokeScript(Fn& body, void* self, ScriptArgs args)
{
    std::size_t cursor = 0;
    if constexpr (sizeof...(Params) == 0) {
        return applyScript<Ret>(body, std::tuple<>{});
    }
    else {
        // Braced-init-list evaluates left to right, so `cursor` follows parameter order.
        std::tuple<Stored<Params>...> bound{takeScript<Params>(self, args, cursor)...};
        (void)cursor;
        return applyScript<Ret>(body, std::move(bound));
    }
}

template <typename Ret, typename... Params, typename Fn>
std::any invokeAny(Fn& body, void* self, const ArgumentList& args)
{
    std::size_t cursor = 0;
    if constexpr (sizeof...(Params) == 0) {
        return applyAny<Ret>(body, std::tuple<>{});
    }
    else {
        std::tuple<Stored<Params>...> bound{takeAny<Params>(self, args, cursor)...};
        (void)cursor;
        return applyAny<Ret>(body, std::move(bound));
    }
}

} // namespace detail

template <typename T>
constexpr bool crossesToScripts()
{
    using D = std::decay_t<T>;
    if constexpr (std::is_integral_v<D> || std::is_floating_point_v<D>) {
        return true;
    }
    else if constexpr (std::is_same_v<D, std::string> || std::is_same_v<D, glm::vec2> || std::is_same_v<D, glm::vec3> ||
                       std::is_same_v<D, glm::vec4> || std::is_same_v<D, ScriptRef> || std::is_same_v<D, ScriptFnHandle> ||
                       std::is_same_v<D, ScriptSelfHandle> || std::is_same_v<D, ScriptRect>) {
        return true;
    }
    else if constexpr (std::is_enum_v<D>) {
        return true;
    }
    else {
        return false;
    }
}

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

namespace detail
{

template <typename Ret, typename... Params, typename Fn>
NativeMethod bindNative(Fn body)
{
    static_assert(nativeSignatureOk<Ret, Params...>(),
                  "script::fn: a parameter does not cross to scripts, or the method has more than one ScriptSelfHandle or object receiver");

    NativeMethod method;
    if constexpr (std::is_void_v<Ret>) {
        method.returnTypeName = "void";
    }
    else {
        method.returnType     = type_index_v<std::decay_t<Ret>>;
        method.returnTypeName = typeid(std::decay_t<Ret>).name();
    }
    method.ownerSlot = nativeOwnerSlot<Params...>();
    if constexpr (sizeof...(Params) > 0) {
        auto add = [&]<typename Param>() {
            if constexpr (!isObjectReceiver<Param>()) {
                method.argTypes.push_back(type_index_v<std::decay_t<Param>>);
                method.argTypeNames.push_back(typeid(std::decay_t<Param>).name());
            }
        };
        (add.template operator()<Params>(), ...);
    }

    method.call = [body](void* self, const ScriptRef&, ScriptArgs args) mutable -> ScriptValue {
        return invokeScript<Ret, Params...>(body, self, args);
    };
    method.invoker = [body](void* self, const ArgumentList& args) mutable -> std::any {
        return invokeAny<Ret, Params...>(body, self, args);
    };
    return method;
}

template <typename F, typename Ret, typename Class, typename... Params>
NativeMethod bindFunctor(F&& functor, Ret (Class::*)(Params...) const)
{
    return bindNative<Ret, Params...>(std::forward<F>(functor));
}

template <typename F, typename Ret, typename Class, typename... Params>
NativeMethod bindFunctor(F&& functor, Ret (Class::*)(Params...))
{
    return bindNative<Ret, Params...>(std::forward<F>(functor));
}

} // namespace detail

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
