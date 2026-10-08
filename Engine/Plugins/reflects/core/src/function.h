#pragma once

#include "meta_value.h"
#include "property.h"

#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

#include "reflects-core/api.h"

// ============================================================================
// Function Type Enumeration
// ============================================================================
enum class FunctionType
{
    MemberFunction,       // 普通成员函数
    ConstMemberFunction,  // const 成员函数
    StaticMemberFunction, // 静态成员函数
    GlobalFunction        // 全局函数
};

namespace refl
{

// 形参 / 返回值。type 是 decay 后的类型；指针取 pointee。
// bRef 只表示非 const 引用。const T& 与按值 const 都是 bConst，不设 bRef。
struct ParamDesc
{
    type_index_t type     = 0;
    bool         bRef     = false;
    bool         bPointer = false;
    bool         bConst   = false;
};

template <typename P>
constexpr ParamDesc describeParam()
{
    using NoRef = std::remove_reference_t<P>;
    ParamDesc desc;
    if constexpr (std::is_pointer_v<NoRef>) {
        using Pointee = std::remove_pointer_t<NoRef>;
        desc.type     = type_index_v<std::remove_cv_t<Pointee>>;
        desc.bPointer = true;
        desc.bConst   = std::is_const_v<Pointee>;
    }
    else if constexpr (std::is_lvalue_reference_v<P> && !std::is_const_v<NoRef>) {
        desc.type = type_index_v<std::remove_cv_t<NoRef>>;
        desc.bRef = true;
    }
    else {
        desc.type   = type_index_v<std::decay_t<P>>;
        desc.bConst = std::is_const_v<NoRef>;
    }
    return desc;
}

template <typename R>
constexpr ParamDesc describeReturn()
{
    if constexpr (std::is_void_v<R>) {
        return {};
    }
    else {
        return describeParam<R>();
    }
}

template <typename T>
const TypeOps &registeredOps()
{
    static const TypeOps *ops = TypeRegistry::instance().find(type_index_v<T>);
    if (ops == nullptr) {
        throw std::runtime_error("arithmetic type is not registered");
    }
    return *ops;
}

// 把一个 MetaValue 绑到形参 P。转换出的临时值活在返回的 BoundArg 里，
// 调用方必须让它活过被调函数返回。
template <typename P>
struct BoundArg
{
    using Raw                 = std::remove_reference_t<P>;
    using T                   = std::decay_t<P>;
    static constexpr bool kPointer = std::is_pointer_v<Raw>;

    std::optional<T> owned;
    T               *view = nullptr;
    Raw              pointer{};

    decltype(auto) get()
    {
        if constexpr (kPointer) {
            return pointer;
        }
        else if constexpr (std::is_lvalue_reference_v<P>) {
            return static_cast<P>(*view);
        }
        else {
            return static_cast<T>(*view);
        }
    }
};

template <typename P>
BoundArg<P> bindArg(MetaValue &arg)
{
    using Raw = std::remove_reference_t<P>;
    using T   = std::decay_t<P>;
    BoundArg<P> bound;

    if constexpr (std::is_pointer_v<Raw>) {
        using Pointee               = std::remove_pointer_t<Raw>;
        using Bare                  = std::remove_cv_t<Pointee>;
        constexpr bool kWantConst   = std::is_const_v<Pointee>;
        if (arg.isNull()) {
            bound.pointer = nullptr;
            return bound;
        }
        if (!arg.isRef() || arg.type() != type_index_v<Bare>) {
            throw std::runtime_error("pointer argument requires a reference or null");
        }
        if constexpr (!kWantConst) {
            if (arg.isConst()) {
                throw std::runtime_error("cannot bind a const reference to a non-const pointer");
            }
        }
        bound.pointer = static_cast<Raw>(arg.data());
        return bound;
    }
    else if constexpr (std::is_lvalue_reference_v<P> && !std::is_const_v<Raw>) {
        if (arg.isNull() || arg.type() != type_index_v<T> || arg.isConst() || arg.data() == nullptr) {
            throw std::runtime_error("cannot bind a const value to a non-const reference");
        }
        T *slot = arg.tryGet<T>();
        if (slot == nullptr) {
            throw std::runtime_error("cannot bind a const value to a non-const reference");
        }
        bound.view = slot;
        return bound;
    }
    else {
        if (!arg.isNull() && arg.type() == type_index_v<T> && arg.data() != nullptr) {
            const T *slot = static_cast<const MetaValue &>(arg).tryGet<T>();
            if (slot == nullptr) {
                throw std::runtime_error("argument type mismatch");
            }
            bound.view = const_cast<T *>(slot);
            return bound;
        }
        constexpr bool kConvertible = std::is_arithmetic_v<T> && !std::is_same_v<T, bool>;
        if constexpr (kConvertible) {
            T stored{};
            bool ok = false;
            if (!arg.isNull() && arg.data() != nullptr) {
                const TypeOps &dst = registeredOps<T>();
                if (arg.type() == type_index_v<int64_t>) {
                    ok = arg.convertTo(registeredOps<int64_t>(), dst, &stored);
                }
                else if (arg.type() == type_index_v<double>) {
                    ok = arg.convertTo(registeredOps<double>(), dst, &stored);
                }
                else {
                    ok = arg.convertTo(type_index_v<T>, &stored);
                }
            }
            if (!ok) {
                throw std::runtime_error("argument conversion failed: value is out of range or not convertible");
            }
            bound.owned = stored;
            bound.view  = &*bound.owned;
            return bound;
        }
        else {
            throw std::runtime_error("argument type mismatch");
        }
    }
}

template <typename Ret, typename Value>
MetaValue boxReturn(Value &&value)
{
    if constexpr (std::is_lvalue_reference_v<Ret>) {
        using T = std::remove_reference_t<Ret>;
        if constexpr (std::is_const_v<T>) {
            return MetaValue::cref(value);
        }
        else {
            return MetaValue::ref(value);
        }
    }
    else if constexpr (std::is_pointer_v<std::remove_reference_t<Ret>>) {
        using Ptr     = std::remove_reference_t<Ret>;
        using Pointee = std::remove_pointer_t<Ptr>;
        using Bare    = std::remove_cv_t<Pointee>;
        if (value == nullptr) {
            return MetaValue::ref(type_index_v<Bare>, nullptr, std::is_const_v<Pointee>);
        }
        if constexpr (std::is_const_v<Pointee>) {
            return MetaValue::cref(*value);
        }
        else {
            return MetaValue::ref(*value);
        }
    }
    else {
        return MetaValue::make(std::forward<Value>(value));
    }
}

template <typename Ret>
Ret takeResult(MetaValue &&result)
{
    if constexpr (std::is_void_v<Ret>) {
        return;
    }
    else if constexpr (std::is_pointer_v<std::remove_reference_t<Ret>>) {
        using Ptr                   = std::remove_reference_t<Ret>;
        using Pointee               = std::remove_pointer_t<Ptr>;
        using Bare                  = std::remove_cv_t<Pointee>;
        constexpr bool kWantConst   = std::is_const_v<Pointee>;
        if (result.isNull()) {
            return nullptr;
        }
        if (!result.isRef() || result.type() != type_index_v<Bare>) {
            throw std::runtime_error("return type mismatch");
        }
        if constexpr (!kWantConst) {
            if (result.isConst()) {
                throw std::runtime_error("return type mismatch");
            }
        }
        return static_cast<Ptr>(result.data());
    }
    else if constexpr (std::is_lvalue_reference_v<Ret>) {
        using T    = std::remove_reference_t<Ret>;
        using Bare = std::remove_cv_t<T>;
        if (!result.isRef() || result.type() != type_index_v<Bare> || result.data() == nullptr) {
            throw std::runtime_error("return type mismatch");
        }
        if constexpr (!std::is_const_v<T>) {
            if (result.isConst()) {
                throw std::runtime_error("return type mismatch");
            }
        }
        return *static_cast<T *>(result.data());
    }
    else {
        using T = std::decay_t<Ret>;
        if (const T *slot = static_cast<const MetaValue &>(result).tryGet<T>()) {
            return *slot;
        }
        constexpr bool kConvertible = std::is_arithmetic_v<T> && !std::is_same_v<T, bool>;
        if constexpr (kConvertible) {
            T stored{};
            if (result.convertTo(type_index_v<T>, &stored)) {
                return stored;
            }
        }
        throw std::runtime_error("return type mismatch");
    }
}

// 类型化 invoke / createInstance 的实参装箱：
// 右值或按值 → MetaValue::make；
// 非 const 左值 → MetaValue::ref（写入落在调用方对象上）；
// const 左值 → MetaValue::cref；
// 指针 T* → 指向 T 的 Ref（空指针是 data()==nullptr 的 Ref），const 随 pointee 走。
template <typename A>
MetaValue boxCallArg(A &&arg)
{
    using Raw = std::remove_reference_t<A>;
    if constexpr (std::is_pointer_v<Raw>) {
        using Pointee = std::remove_pointer_t<Raw>;
        using Bare    = std::remove_cv_t<Pointee>;
        if (arg == nullptr) {
            return MetaValue::ref(type_index_v<Bare>, nullptr, std::is_const_v<Pointee>);
        }
        if constexpr (std::is_const_v<Pointee>) {
            return MetaValue::cref(*arg);
        }
        else {
            return MetaValue::ref(*arg);
        }
    }
    else if constexpr (std::is_lvalue_reference_v<A>) {
        if constexpr (std::is_const_v<Raw>) {
            return MetaValue::cref(arg);
        }
        else {
            return MetaValue::ref(arg);
        }
    }
    else {
        return MetaValue::make(std::forward<A>(arg));
    }
}

} // namespace refl

// ============================================================================
// MARK: Function
// ============================================================================
struct Function : public Field
{
    FunctionType type = FunctionType::MemberFunction;

    // 成员函数的 self 是对象；静态 / 全局函数的 self 是 nullptr。
    // args 指向调用方栈上的数组，可写：T& / 输出参数的写入落在元素自身或其借用目标上。
    std::function<refl::MetaValue(void *, std::span<refl::MetaValue>)> invoker;

    size_t                   argCount = 0;
    /// Declared `args` are the required prefix. Further arguments are forwarded
    /// as already-built MetaValues; `invoke` does not enforce `argCount`.
    bool                     bVariadic = false;
    std::vector<std::string> argTypeNames;
    std::string              returnTypeName;
    // 与 invoker 的 args 一一对应。void 返回的 ret.type 为 0。
    std::vector<refl::ParamDesc> args;
    refl::ParamDesc              ret;

    bool isStatic() const
    {
        return type == FunctionType::StaticMemberFunction ||
               type == FunctionType::GlobalFunction;
    }

    bool isConst() const { return type == FunctionType::ConstMemberFunction; }
};

// ============================================================================
// Detail namespace - Implementation helpers
// ============================================================================
namespace detail
{

template <typename T>
std::string getTypeName()
{
    return typeid(T).name();
}

template <typename ClassType, typename Ret, typename... Args>
refl::MetaValue memberFunctionWrapper(ClassType *obj, Ret (ClassType::*func)(Args...),
                                      std::span<refl::MetaValue> args)
{
    if (args.size() != sizeof...(Args)) {
        throw std::runtime_error("Argument count mismatch");
    }

    if constexpr (sizeof...(Args) == 0) {
        if constexpr (std::is_void_v<Ret>) {
            (obj->*func)();
            return {};
        }
        else {
            return refl::boxReturn<Ret>((obj->*func)());
        }
    }
    else {
        auto call = [&]<size_t... I>(std::index_sequence<I...>) -> refl::MetaValue {
            std::tuple<refl::BoundArg<Args>...> bound{refl::bindArg<Args>(args[I])...};
            if constexpr (std::is_void_v<Ret>) {
                (obj->*func)(std::get<I>(bound).get()...);
                return {};
            }
            else {
                return refl::boxReturn<Ret>((obj->*func)(std::get<I>(bound).get()...));
            }
        };
        return call(std::index_sequence_for<Args...>{});
    }
}

template <typename ClassType, typename Ret, typename... Args>
refl::MetaValue constMemberFunctionWrapper(const ClassType *obj,
                                           Ret (ClassType::*func)(Args...) const,
                                           std::span<refl::MetaValue> args)
{
    if (args.size() != sizeof...(Args)) {
        throw std::runtime_error("Argument count mismatch");
    }

    if constexpr (sizeof...(Args) == 0) {
        if constexpr (std::is_void_v<Ret>) {
            (obj->*func)();
            return {};
        }
        else {
            return refl::boxReturn<Ret>((obj->*func)());
        }
    }
    else {
        auto call = [&]<size_t... I>(std::index_sequence<I...>) -> refl::MetaValue {
            std::tuple<refl::BoundArg<Args>...> bound{refl::bindArg<Args>(args[I])...};
            if constexpr (std::is_void_v<Ret>) {
                (obj->*func)(std::get<I>(bound).get()...);
                return {};
            }
            else {
                return refl::boxReturn<Ret>((obj->*func)(std::get<I>(bound).get()...));
            }
        };
        return call(std::index_sequence_for<Args...>{});
    }
}

template <typename Ret, typename... Args>
refl::MetaValue staticFunctionWrapper(Ret (*func)(Args...), std::span<refl::MetaValue> args)
{
    if (args.size() != sizeof...(Args)) {
        throw std::runtime_error("Argument count mismatch");
    }

    if constexpr (sizeof...(Args) == 0) {
        if constexpr (std::is_void_v<Ret>) {
            func();
            return {};
        }
        else {
            return refl::boxReturn<Ret>(func());
        }
    }
    else {
        auto call = [&]<size_t... I>(std::index_sequence<I...>) -> refl::MetaValue {
            std::tuple<refl::BoundArg<Args>...> bound{refl::bindArg<Args>(args[I])...};
            if constexpr (std::is_void_v<Ret>) {
                func(std::get<I>(bound).get()...);
                return {};
            }
            else {
                return refl::boxReturn<Ret>(func(std::get<I>(bound).get()...));
            }
        };
        return call(std::index_sequence_for<Args...>{});
    }
}

} // namespace detail
