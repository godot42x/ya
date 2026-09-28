#pragma once

// Lua listeners on C++ multicast delegates.
//
//   local h = widget.onClicked:add(self, self.onRestart)   -- fn(self, ...)
//   widget.onClicked:remove(h)
//   widget.onClicked:removeAll(self)
//
// `self` must be the `self` of a live script instance: every listener has
// exactly one owner. The delegate holds only a weak token; the Lua function
// lives in the owner's LuaListenerScope, which goes with the instance's `self`
// (LuaScriptInstance::releaseLuaHandles, or a hot reload binding a new one).
// So a listener never outlives its script, and no Lua reference sits in a
// delegate that may outlive the Lua state.
//
// Only events a binding exposes are reachable, and their source must be held
// by shared_ptr so the event can tell when it is gone.

#include "Core/Api.h"
#include "Core/Delegate.h"
#include "Core/Log.h"

#include <sol/sol.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

namespace ya
{

/// The Lua callbacks one script instance handed to delegates.
struct LuaListenerScope
{
    struct FCallback
    {
        sol::protected_function fn;
        const char*             event = "";
    };

    sol::table  self;
    std::string scriptPath;

  private:
    std::unordered_map<uint32_t, FCallback> _callbacks;
    uint32_t                                _nextSlot = 1;

  public:
    /// The scope of the live script instance whose `self` this is, or null.
    static YA_ECS_SYSTEMS_API std::shared_ptr<LuaListenerScope> of(const sol::object& self);
    /// A fresh scope for a newly bound `self`, recorded on it.
    static YA_ECS_SYSTEMS_API std::shared_ptr<LuaListenerScope> bind(sol::table& self, const std::string& scriptPath);
    /// Register the hidden `self` field type and the no-argument `Event`.
    static YA_ECS_SYSTEMS_API void registerTypes(sol::state& lua);

    YA_ECS_SYSTEMS_API uint32_t hold(sol::protected_function fn, const char* event);
    YA_ECS_SYSTEMS_API void     drop(uint32_t slot);

    template <typename... Args>
    void call(uint32_t slot, Args&&... args)
    {
        const auto it = _callbacks.find(slot);
        if (it == _callbacks.end()) {
            return;
        }
        // Copied: the callback may add or drop listeners of this scope.
        const FCallback                      callback = it->second;
        const sol::protected_function_result result   = callback.fn(self, std::forward<Args>(args)...);
        if (!result.valid()) {
            const sol::error error = result;
            YA_CORE_ERROR("Lua {} listener error ({}): {}", callback.event, scriptPath, error.what());
        }
    }
};

/// What a delegate holds for one Lua listener: the scope weakly and a slot.
/// The last copy going away (remove, or the delegate dying) frees the slot.
struct FLuaListenerToken
{
    std::weak_ptr<LuaListenerScope> scope;
    uint32_t                        slot = 0;

    FLuaListenerToken(std::weak_ptr<LuaListenerScope> inScope, uint32_t inSlot) : scope(std::move(inScope)), slot(inSlot) {}
    FLuaListenerToken(const FLuaListenerToken&)            = delete;
    FLuaListenerToken& operator=(const FLuaListenerToken&) = delete;
    ~FLuaListenerToken()
    {
        if (auto live = scope.lock()) {
            live->drop(slot);
        }
    }
};

/// A delegate member of a shared_ptr-owned object, as seen from Lua.
template <typename... Args>
struct TLuaEvent
{
    using DelegateType = MulticastDelegate<void(Args...)>;

    std::weak_ptr<const void> source;
    DelegateType*             delegate = nullptr;
    const char*               name     = "";

    [[nodiscard]] DelegateType* live() const
    {
        if (source.expired() || !delegate) {
            YA_CORE_WARN("Lua {}: its source is gone; ignored", name);
            return nullptr;
        }
        return delegate;
    }

    DelegateHandle add(const sol::object& target, const sol::object& fn) const
    {
        DelegateType* d = live();
        if (!d) {
            return INVALID_HANDLE;
        }
        std::shared_ptr<LuaListenerScope> scope = LuaListenerScope::of(target);
        if (!scope) {
            YA_CORE_ERROR("Lua {}:add: the owner must be a live script's self", name);
            return INVALID_HANDLE;
        }
        if (fn.get_type() != sol::type::function) {
            YA_CORE_ERROR("Lua {}:add ({}): callback is not a function", name, scope->scriptPath);
            return INVALID_HANDLE;
        }
        auto token = std::make_shared<FLuaListenerToken>(scope, scope->hold(fn.as<sol::protected_function>(), name));
        return d->addWeakLambda(std::weak_ptr<LuaListenerScope>(scope), [token](Args... args) {
            if (auto live = token->scope.lock()) {
                live->call(token->slot, args...);
            }
        });
    }

    bool remove(DelegateHandle handle) const
    {
        DelegateType* d = live();
        return d && d->remove(handle);
    }

    size_t removeAll(const sol::object& target) const
    {
        DelegateType*                     d     = live();
        std::shared_ptr<LuaListenerScope> scope = d ? LuaListenerScope::of(target) : nullptr;
        return scope ? d->removeAll(scope.get()) : 0;
    }

    /// Register this signature's Lua type once per state.
    static void registerType(sol::state& lua, const char* typeName)
    {
        lua.new_usertype<TLuaEvent>(typeName,
                                    sol::no_constructor,
                                    "add",
                                    &TLuaEvent::add,
                                    "remove",
                                    &TLuaEvent::remove,
                                    "removeAll",
                                    &TLuaEvent::removeAll);
    }
};

/// `owner->*member` as a Lua event; an empty owner gives an event whose
/// methods warn and do nothing.
template <typename Owner, typename... Args>
[[nodiscard]] TLuaEvent<Args...> makeLuaEvent(const std::shared_ptr<Owner>& owner,
                                              MulticastDelegate<void(Args...)> Owner::*member,
                                              const char*                              name)
{
    return TLuaEvent<Args...>{
        .source   = owner,
        .delegate = owner ? &((*owner).*member) : nullptr,
        .name     = name,
    };
}

} // namespace ya
