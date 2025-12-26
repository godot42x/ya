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

using DelegateHandle                                  = size_t;
static inline constexpr DelegateHandle INVALID_HANDLE = 0;

template <typename... Args>
class MulticastDelegate<void(Args...)>
{
  public:
    using FunctionType = std::function<void(Args...)>;

    MulticastDelegate() = default;

    struct FunctorImpl
    {
        DelegateHandle        handle;
        std::optional<void *> caller;
        FunctionType          func;
    };

  private:
    std::vector<FunctorImpl> m_Functions;
    DelegateHandle           m_NextHandle = 1; // 0 reserved for invalid handle

    DelegateHandle generateHandle()
    {
        return m_NextHandle++;
    }

  public:

    // Add static function, returns handle for removal
    DelegateHandle addStatic(const FunctionType &function)
    {
        DelegateHandle handle = generateHandle();
        m_Functions.push_back({
            .handle = handle,
            .caller = {},
            .func   = function,
        });
        return handle;
    }

    // Add member function, returns handle for removal
    template <typename Obj>
    DelegateHandle addObject(Obj *obj, void (Obj::*member_func)(Args...))
    {
        DelegateHandle handle = generateHandle();
        m_Functions.push_back({
            .handle = handle,
            .caller = obj,
            .func   = [obj, member_func](Args... args) {
                (obj->*member_func)(std::forward<Args>(args)...);
            },
        });
        return handle;
    }

    // Add lambda without owner, returns handle for removal
    DelegateHandle addLambda(std::function<void(Args...)> lambda)
    {
        DelegateHandle handle = generateHandle();
        m_Functions.push_back({
            .handle = handle,
            .caller = {},
            .func   = lambda,
        });
        return handle;
    }

    // Add lambda with owner pointer, returns handle for removal
    template <typename Obj>
    DelegateHandle addLambda(Obj *ptr, std::function<void(Args...)> lambda)
    {
        DelegateHandle handle = generateHandle();
        m_Functions.push_back({
            .handle = handle,
            .caller = ptr,
            .func   = lambda,
        });
        return handle;
    }

    // TODO: use map for faster removal?
    // Remove delegate by handle
    bool remove(DelegateHandle handle)
    {
        if (handle == INVALID_HANDLE) {
            return false;
        }

        auto it = std::find_if(m_Functions.begin(), m_Functions.end(), [handle](const FunctorImpl &item) {
            return item.handle == handle;
        });

        if (it != m_Functions.end()) {
            m_Functions.erase(it);
            return true;
        }
        return false;
    }

    // Remove all delegates owned by specific object
    size_t removeAll(void *owner)
    {
        size_t removedCount = 0;
        auto   it           = std::remove_if(m_Functions.begin(), m_Functions.end(), [owner, &removedCount](const FunctorImpl &item) {
            if (item.caller.has_value() && item.caller.value() == owner) {
                ++removedCount;
                return true;
            }
            return false;
        });
        m_Functions.erase(it, m_Functions.end());
        return removedCount;
    }

    // Clear all delegates
    void clear()
    {
        m_Functions.clear();
    }

    // Get number of bound delegates
    size_t size() const
    {
        return m_Functions.size();
    }

    // Broadcast to all delegates
    void broadcast(Args... args)
    {
        // Remove delegates with null owner pointers (auto cleanup)
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