#pragma once

#include <functional>
#include <memory>
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

    void Set(const FunctionType &function)
    {
        m_Function = function;
        bBound     = true;
    }

    // Execute the delegate with arguments
    ReturnType operator()(Args... args) const
    {
        return m_Function(std::forward<Args>(args)...);
    }

    ReturnType ExecuteIfBound(Args &&...args) const
    {
        if (bBound) {
            return m_Function(std::forward<Args>(args)...);
        }
    }

  private:
    FunctionType m_Function;
    bool         bBound = false;
};

template <typename... Args>
class MulticastDelegate
{
  public:
    using FunctionType = std::function<void(Args...)>;

    MulticastDelegate() {};

    void AddStatic(const FunctionType &function) { m_Functions.push_back(function); }


    template <typename Obj>
    void AddObject(Obj *obj, void (Obj::*member_func)(Args...))
    {
        m_Functions.push_back([obj, member_func](Args... args) {
            (obj->*member_func)(std::forward<Args>(args)...);
        });
    }

    template <typename Obj>
    void AddWeak(Obj *obj, void (Obj::*member_func)(Args...))
    {
        std::weak_ptr<Obj> weak_obj(obj);
        m_Functions.push_back([weak_obj, member_func](Args... args) {
            if (auto object_ptr = weak_obj.lock()) {
                (object_ptr.get()->*member_func)(std::forward<Args>(args)...);
            }
        });
    }


    void Broadcast(Args... args) const
    {
        for (const auto &func : m_Functions) {
            func(std::forward<Args>(args)...);
        }
    }

  private:
    std::vector<FunctionType> m_Functions;
};