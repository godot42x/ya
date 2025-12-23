#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>


template <typename Signature>
class Delegate;

template <typename ReturnType, typename... Args>
class Delegate<ReturnType(Args...)>
{
  public:
    using FunctionType = std::function<ReturnType(Args...)>;

    Delegate() {};
    Delegate(const FunctionType &function) : m_Function(function), bBound(true) {}

    void set(const FunctionType &function)
    {
        m_Function = function;
        bBound     = true;
    }
    void set(FunctionType &&function)
    {
        m_Function = std::move(function);
        bBound     = true;
    }

    // Execute the delegate with arguments
    ReturnType operator()(Args... args) const
    {
        return m_Function(std::forward<Args>(args)...);
    }

    // NOTICE: Arg already be deduced  when this func called, so Args&& only receive rvalues
    // ReturnType executeIfBound(const Args&&... args) const
    // Solution: Accept lvalues and rvalues - use const reference for safety
    ReturnType executeIfBound(const Args &...args) const
    {
        if (bBound) {
            return m_Function(args...);
        }
        if constexpr (std::is_void_v<ReturnType>) {
            return; // No return value for void
        }
        return ReturnType{}; // Return default value if not bound
    }

    void isBound(bool bound)
    {
        bBound = bound;
    }
    void unbind()
    {
        bBound     = false;
        m_Function = nullptr;
    }

  private:
    FunctionType m_Function;
    bool         bBound = false;
};


template <typename Signature>
class MulticastDelegate;

template <typename... Args>
class MulticastDelegate<void(Args...)>
{
  public:
    using FunctionType = std::function<void(Args...)>;

    MulticastDelegate() = default;

    struct FunctorImpl
    {
        std::optional<void *> caller;
        FunctionType          func;
    };

  private:
    std::vector<FunctorImpl> m_Functions;

  public:
    void addStatic(const FunctionType &function)
    {
        m_Functions.push_back({
            .caller = {},
            .func   = function,
        });
    }


    template <typename Obj>
    void addObject(Obj *obj, void (Obj::*member_func)(Args...))
    {
        m_Functions.push_back({
            .caller = obj,
            .func   = [obj, member_func](Args... args) {
                (obj->*member_func)(std::forward<Args>(args)...);
            },
        });
    }
    void addLambda(std::function<void(Args...)> lambda)
    {
        m_Functions.push_back({
            .caller = {},
            .func   = lambda,
        });
    }

    template <typename Obj>
    void addLambda(Obj *ptr, std::function<void(Args...)> lambda)
    {
        m_Functions.push_back({
            .caller = ptr,
            .func   = lambda,
        });
    }

    // void remove()


    void broadcast(Args... args)
    {
        m_Functions.erase(
            std::remove_if(m_Functions.begin(), m_Functions.end(), [](const FunctorImpl &item) {
                return item.caller.has_value() && item.caller.value() == nullptr;
            }),
            m_Functions.end());

        for (const auto &item : m_Functions)
        {
            item.func(std::forward<Args>(args)...);
        }
    }
};