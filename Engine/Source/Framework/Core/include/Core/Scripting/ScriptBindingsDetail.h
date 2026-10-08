#pragma once

// internal，勿直接 include.
// ScriptBindings.h includes this after the public predicates are declared.
// Conversion, argument binding, and trampoline construction for script::fn.

#include "Core/Scripting/ScriptValue.h"

#include <reflects-core/lib.h>

#include <functional>
#include <span>
#include <string>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <utility>

namespace ya::script::detail
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
        if (const ScriptRef* ref = scriptIf<ScriptRef>(value)) {
            return *ref;
        }
        if (value.isNull()) {
            return {};
        }
        throw ScriptError(std::string("expected an object, got ") + scriptValueTypeName(value));
    }
    else if constexpr (std::is_enum_v<T>) {
        return static_cast<T>(scriptToEnum(type_index_v<T>, value));
    }
    else if constexpr (std::is_same_v<T, ScriptSelfHandle> || std::is_same_v<T, ScriptFnHandle>) {
        if (const T* held = scriptIf<T>(value)) {
            return *held;
        }
        throw ScriptError(std::string("expected a script handle, got ") + scriptValueTypeName(value));
    }
    else {
        if (const T* held = scriptIf<T>(value)) {
            return *held;
        }
        throw ScriptError(std::string("expected a vector, got ") + scriptValueTypeName(value));
    }
}

template <typename T>
ScriptValue toScript(const T& value)
{
    if constexpr (std::is_same_v<T, bool>) {
        return scriptMake(value);
    }
    else if constexpr (std::is_integral_v<T>) {
        return scriptMake(static_cast<int64_t>(value));
    }
    else if constexpr (std::is_floating_point_v<T>) {
        return scriptMake(static_cast<double>(value));
    }
    else if constexpr (std::is_same_v<T, ScriptRef>) {
        return value ? scriptMake(value) : ScriptValue{};
    }
    else if constexpr (std::is_enum_v<T>) {
        return scriptMake(static_cast<int64_t>(value));
    }
    else {
        return scriptMake(value);
    }
}

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
decltype(auto) takeMeta(void* self, std::span<refl::MetaValue> args, std::size_t& cursor)
{
    if constexpr (isObjectReceiver<Param>()) {
        return bindReceiver<Param>(self);
    }
    else {
        return refl::bindArg<Param>(args[cursor++]);
    }
}

template <typename Param>
constexpr bool receiverIsConst()
{
    if constexpr (!isObjectReceiver<Param>()) {
        return false;
    }
    else {
        using NoRef = std::remove_reference_t<Param>;
        if constexpr (std::is_pointer_v<NoRef>) {
            return std::is_const_v<std::remove_pointer_t<NoRef>>;
        }
        else {
            return std::is_const_v<NoRef>;
        }
    }
}

template <typename Param>
using MetaStored = std::conditional_t<isObjectReceiver<Param>(), Param, refl::BoundArg<Param>>;

template <typename Param, typename Stored>
decltype(auto) unwrap(Stored&& stored)
{
    if constexpr (isObjectReceiver<Param>()) {
        return std::forward<Stored>(stored);
    }
    else {
        return stored.get();
    }
}

template <typename Ret, typename... Params>
struct CallSig
{
};

template <typename Ret, typename... Params, typename Fn, typename Tuple, std::size_t... I>
refl::MetaValue applyMeta(CallSig<Ret, Params...>, Fn& body, Tuple& bound, std::index_sequence<I...>)
{
    if constexpr (std::is_void_v<Ret>) {
        body(unwrap<Params>(std::get<I>(bound))...);
        return {};
    }
    else {
        return refl::boxReturn<Ret>(body(unwrap<Params>(std::get<I>(bound))...));
    }
}

template <typename Ret, typename... Params, typename Fn>
refl::MetaValue invokeMeta(Fn& body, void* self, std::span<refl::MetaValue> args)
{
    constexpr std::size_t kScriptArgs = ((isObjectReceiver<Params>() ? std::size_t{0} : std::size_t{1}) + ... + std::size_t{0});
    if (args.size() != kScriptArgs) {
        throw std::runtime_error("Argument count mismatch");
    }
    if constexpr (sizeof...(Params) == 0) {
        if constexpr (std::is_void_v<Ret>) {
            body();
            return {};
        }
        else {
            return refl::boxReturn<Ret>(body());
        }
    }
    else {
        std::size_t cursor = 0;
        std::tuple<MetaStored<Params>...> bound{takeMeta<Params>(self, args, cursor)...};
        (void)cursor;
        return applyMeta(CallSig<Ret, Params...>{}, body, bound, std::index_sequence_for<Params...>{});
    }
}

template <typename Ret, typename... Params, typename Fn>
NativeMethod bindNative(Fn body)
{
    static_assert(nativeSignatureOk<Ret, Params...>(),
                  "script::fn: a parameter does not cross to scripts, or the method has more than one ScriptSelfHandle or object receiver");

    NativeMethod method;
    method.ret            = refl::describeReturn<Ret>();
    method.returnTypeName = std::is_void_v<Ret> ? "void" : typeid(std::decay_t<Ret>).name();
    method.ownerSlot      = nativeOwnerSlot<Params...>();
    if constexpr (sizeof...(Params) > 0) {
        method.bConst = (receiverIsConst<Params>() || ...);
        auto add = [&]<typename Param>() {
            if constexpr (!isObjectReceiver<Param>()) {
                method.args.push_back(refl::describeParam<Param>());
                method.argTypeNames.push_back(typeid(std::decay_t<Param>).name());
            }
        };
        (add.template operator()<Params>(), ...);
    }

    method.invoker = [body](void* self, std::span<refl::MetaValue> args) mutable -> refl::MetaValue {
        return invokeMeta<Ret, Params...>(body, self, args);
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

/// `Prefix` is metadata for the language backend. The body receives the
/// argument span unchanged, so a nil slot stays null instead of failing a
/// typed bind.
template <typename Receiver, typename... Prefix, typename F>
NativeMethod bindVariadic(F&& functor)
{
    static_assert(isObjectReceiver<Receiver>(), "script::fnVariadic: the receiver must be a class reference or pointer");
    static_assert((crossesToScripts<std::decay_t<Prefix>>() && ... && true),
                  "script::fnVariadic: a declared argument does not cross to scripts");

    NativeMethod method;
    method.bVariadic      = true;
    method.bConst         = receiverIsConst<Receiver>();
    method.ret            = refl::describeReturn<ScriptValue>();
    method.returnTypeName = "ScriptValue";
    if constexpr (sizeof...(Prefix) > 0) {
        method.args = {refl::describeParam<Prefix>()...};
        (method.argTypeNames.push_back(typeid(std::decay_t<Prefix>).name()), ...);
    }

    method.invoker = [body = std::forward<F>(functor)](void* self, std::span<refl::MetaValue> args) -> refl::MetaValue {
        using Object = std::remove_reference_t<Receiver>;
        const ScriptArgs view(args.data(), args.size());
        return refl::boxReturn<ScriptValue>(body(*static_cast<Object*>(self), view));
    };
    return method;
}

} // namespace ya::script::detail
