#include "ECS/Systems/LuaEvent.h"

namespace ya
{

namespace
{

constexpr const char* kScopeField = "__listeners";

/// What `self.__listeners` holds: the way back from `self` to its scope.
struct FLuaListenerScopeRef
{
    std::weak_ptr<LuaListenerScope> scope;
};

} // namespace

std::shared_ptr<LuaListenerScope> LuaListenerScope::of(const sol::object& self)
{
    if (self.get_type() != sol::type::table) {
        return nullptr;
    }
    const sol::table                           table = self.as<sol::table>();
    const sol::optional<FLuaListenerScopeRef&> ref   = table.raw_get<sol::optional<FLuaListenerScopeRef&>>(kScopeField);
    return ref ? ref->scope.lock() : nullptr;
}

std::shared_ptr<LuaListenerScope> LuaListenerScope::bind(sol::table& self, const std::string& scriptPath)
{
    auto scope        = std::make_shared<LuaListenerScope>();
    scope->self       = self;
    scope->scriptPath = scriptPath;
    self.raw_set(kScopeField, FLuaListenerScopeRef{.scope = scope});
    return scope;
}

void LuaListenerScope::registerTypes(sol::state& lua)
{
    lua.new_usertype<FLuaListenerScopeRef>("__LuaListenerScope", sol::no_constructor);
    TLuaEvent<>::registerType(lua, "Event");
}

uint32_t LuaListenerScope::hold(sol::protected_function fn, const char* event)
{
    const uint32_t slot = _nextSlot++;
    _callbacks.emplace(slot, FCallback{.fn = std::move(fn), .event = event});
    return slot;
}

void LuaListenerScope::drop(uint32_t slot)
{
    _callbacks.erase(slot);
}

} // namespace ya
