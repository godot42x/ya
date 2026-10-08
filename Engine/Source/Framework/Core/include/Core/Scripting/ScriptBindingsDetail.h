#pragma once

// internal，勿直接 include.
// ScriptBindings.h includes this after the public predicates are declared.
// Conversion, argument binding, and trampoline construction for script::fn.

#include "Core/Scripting/ScriptValue.h"

#include <reflects-core/lib.h>

#include <any>
#include <functional>
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

} // namespace ya::script::detail
