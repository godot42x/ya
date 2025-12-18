#pragma once

#include "argument_list.h"
#include "property.h"

#include <any>
#include <functional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

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

// ============================================================================
// MARK: Function
// ============================================================================
struct Function : public Field
{
    FunctionType type = FunctionType::MemberFunction;

    // 统一的函数调用接口
    // 对于成员函数：第一个参数是 this 指针
    // 对于静态/全局函数：第一个参数为 nullptr
    std::function<std::any(void *, const ArgumentList &)> invoker;

    // 元数据
    size_t                   argCount = 0;
    std::vector<std::string> argTypeNames;
    std::string              returnTypeName;

    // 是否是静态函数
    bool isStatic() const
    {
        return type == FunctionType::StaticMemberFunction ||
               type == FunctionType::GlobalFunction;
    }

    // 是否是 const 成员函数
    bool isConst() const { return type == FunctionType::ConstMemberFunction; }
};

// ============================================================================
// Detail namespace - Implementation helpers
// ============================================================================
namespace detail
{

// 类型名称提取
template <typename T>
std::string getTypeName()
{
    return typeid(T).name();
}

// 普通成员函数包装器
template <typename ClassType, typename Ret, typename... Args>
std::any memberFunctionWrapper(ClassType          *obj, Ret (ClassType::*func)(Args...),
                               const ArgumentList &args)
{
    if (args.size() != sizeof...(Args)) {
        throw std::runtime_error("Argument count mismatch");
    }

    auto invokeFunc = [obj, func, &args]<size_t... I>(std::index_sequence<I...>) -> Ret {
        return (obj->*func)(args.get<Args>(I)...);
    };

    if constexpr (std::is_void_v<Ret>) {
        invokeFunc(std::index_sequence_for<Args...>{});
        return std::any();
    }
    else {
        Ret result = invokeFunc(std::index_sequence_for<Args...>{});
        return std::any(result);
    }
}

// const 成员函数包装器
template <typename ClassType, typename Ret, typename... Args>
std::any constMemberFunctionWrapper(const ClassType *obj,
                                    Ret (ClassType::*func)(Args...) const,
                                    const ArgumentList &args)
{
    if (args.size() != sizeof...(Args)) {
        throw std::runtime_error("Argument count mismatch");
    }

    auto invokeFunc = [obj, func, &args]<size_t... I>(std::index_sequence<I...>) -> Ret {
        return (obj->*func)(args.get<Args>(I)...);
    };

    if constexpr (std::is_void_v<Ret>) {
        invokeFunc(std::index_sequence_for<Args...>{});
        return std::any();
    }
    else {
        Ret result = invokeFunc(std::index_sequence_for<Args...>{});
        return std::any(result);
    }
}

// 静态/全局函数包装器
template <typename Ret, typename... Args>
std::any staticFunctionWrapper(Ret (*func)(Args...), const ArgumentList &args)
{
    if (args.size() != sizeof...(Args)) {
        throw std::runtime_error("Argument count mismatch");
    }

    auto invokeFunc = [func, &args]<size_t... I>(std::index_sequence<I...>) -> Ret {
        return func(args.get<Args>(I)...);
    };

    if constexpr (std::is_void_v<Ret>) {
        invokeFunc(std::index_sequence_for<Args...>{});
        return std::any();
    }
    else {
        Ret result = invokeFunc(std::index_sequence_for<Args...>{});
        return std::any(result);
    }
}

} // namespace detail
