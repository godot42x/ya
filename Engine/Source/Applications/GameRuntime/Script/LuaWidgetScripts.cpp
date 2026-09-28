#include "LuaWidgetScripts.h"

#include "LuaWidgetHandle.h"

#include "Core/Log.h"

#include "GameRuntime/GUI/GameUI/GameUIHost.h"

#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/UIElement.h"

#include <algorithm>

namespace ya
{

namespace
{

/// What `self.__widgetScript` holds: the way back from a script's `self` to
/// the behaviour that hosts it.
struct FWidgetScriptRef
{
    std::weak_ptr<LuaWidgetScriptBehavior> behavior;
};

/// What `button:onClick` returns. Holds no Lua reference and no runtime
/// pointer, so it is safe to keep past either.
struct FLuaUIConnection
{
    std::weak_ptr<UIButton> button;
    DelegateHandle          handle = INVALID_HANDLE;

    void disconnect()
    {
        if (auto live = button.lock(); live && handle != INVALID_HANDLE) {
            live->onClicked.remove(handle);
        }
        handle = INVALID_HANDLE;
    }
};

struct FLuaUITimer
{
    GameUIHost* host = nullptr;
    uint64_t    id   = 0;

    void cancel()
    {
        if (host && id != 0) {
            host->cancelTimer(id);
        }
        id = 0;
    }
};

struct FWidgetScriptHost final : ILuaScriptHost
{
    std::weak_ptr<LuaWidgetScriptBehavior> behavior;

    explicit FWidgetScriptHost(std::weak_ptr<LuaWidgetScriptBehavior> inBehavior) : behavior(std::move(inBehavior)) {}

    LuaScriptInstance* resolve(uint64_t instanceId) override
    {
        auto live = behavior.lock();
        return live && live->instance.runtimeId == instanceId ? &live->instance : nullptr;
    }

    void bindSelf(sol::table& self) override
    {
        if (auto live = behavior.lock(); live && live->runtime) {
            live->runtime->bindSelf(live, self);
        }
    }
};

std::shared_ptr<LuaWidgetScriptBehavior> behaviorOf(const sol::table& self)
{
    const sol::optional<FWidgetScriptRef&> ref = self.raw_get<sol::optional<FWidgetScriptRef&>>("__widgetScript");
    return ref ? ref->behavior.lock() : nullptr;
}

bool sameTable(const sol::table& a, const sol::table& b)
{
    if (!a.valid() || !b.valid()) {
        return false;
    }
    lua_State* L = a.lua_state();
    a.push(L);
    b.push(L);
    const bool bSame = lua_rawequal(L, -1, -2) != 0;
    lua_pop(L, 2);
    return bSame;
}

sol::object addScriptTimer(const sol::table& self, float delaySeconds, float intervalSeconds,
                           const sol::protected_function& fn, sol::this_state state)
{
    sol::state_view lua(state);
    auto            behavior = behaviorOf(self);
    if (!behavior || !behavior->runtime) {
        return sol::make_object(lua, sol::lua_nil);
    }
    if (!fn.valid()) {
        YA_CORE_WARN("Lua widget timer ({}): callback is not a function", behavior->instance.scriptPath);
        return sol::make_object(lua, sol::lua_nil);
    }
    GameUIHost& host = behavior->runtime->host;
    // A timer belongs to the instance that made it: once that instance is
    // destroyed or replaced by a hot reload (a new `self`), it stops.
    const uint64_t id = host.addTimer(
        behavior.get(), delaySeconds, intervalSeconds,
        [weak = std::weak_ptr(behavior), fn, owner = self, path = behavior->instance.scriptPath]() -> bool {
            auto live = weak.lock();
            if (!live || !live->runtime || !live->instance.bLoaded || !sameTable(live->instance.self, owner)) {
                return false;
            }
            const sol::protected_function_result result = fn(owner);
            if (!result.valid()) {
                const sol::error error = result;
                YA_CORE_ERROR("Lua widget timer error ({}): {}", path, error.what());
            }
            return true;
        });
    return sol::make_object(lua, FLuaUITimer{.host = &host, .id = id});
}

} // namespace

LuaWidgetScriptBehavior::~LuaWidgetScriptBehavior()
{
    if (runtime) {
        runtime->release(*this);
    }
}

void LuaWidgetScriptBehavior::tick(UIElement& owner, float deltaSeconds)
{
    (void)owner;
    if (runtime) {
        runtime->scripting.call(instance, ELuaScriptCallback::Update, deltaSeconds);
    }
}

void LuaWidgetScriptBehavior::onDetached(UIElement& owner)
{
    UIBehavior::onDetached(owner);
    if (runtime) {
        runtime->release(*this);
    }
}

LuaWidgetScripts::LuaWidgetScripts(LuaScriptingSystem& inScripting, GameUIHost& inHost)
    : scripting(inScripting), host(inHost)
{
    sol::state& lua = scripting.lua();
    lua.new_usertype<FWidgetScriptRef>("__WidgetScriptRef", sol::no_constructor);
    lua.new_usertype<FLuaUITimer>("UITimer", sol::no_constructor, "cancel", &FLuaUITimer::cancel);
    lua.new_usertype<FLuaUIConnection>("UIConnection", sol::no_constructor, "disconnect", &FLuaUIConnection::disconnect);

    bindLuaWidgetHandles(lua);
    sol::usertype<LuaButtonHandle> buttonType = lua["Button"];
    buttonType.set_function("onClick", [this](const LuaButtonHandle& button, const sol::object& target,
                                              const sol::protected_function& fn, sol::this_state state) {
        return connectClick(button, target, fn, state);
    });

    _selfApi = lua.create_table();
    _selfApi.set_function("setTickEnabled", [](const sol::table& self, bool bEnabled) {
        if (auto behavior = behaviorOf(self)) {
            behavior->bTickEnabled = bEnabled;
        }
    });
    _selfApi.set_function("after", [](const sol::table& self, float seconds, const sol::protected_function& fn,
                                      sol::this_state state) { return addScriptTimer(self, seconds, 0.0f, fn, state); });
    _selfApi.set_function("every", [](const sol::table& self, float seconds, const sol::protected_function& fn,
                                      sol::this_state state) { return addScriptTimer(self, seconds, seconds, fn, state); });
    _selfApi.set_function("find", [](const sol::table& self, const std::string& name, sol::this_state state) -> sol::object {
        auto         behavior = behaviorOf(self);
        UIElementRef root     = behavior ? behavior->entryRoot.lock() : nullptr;
        if (!root || !behavior->runtime) {
            return sol::make_object(state, sol::lua_nil);
        }
        GameUIHost& host = behavior->runtime->host;
        return makeLuaWidgetHandle(state, host, host.findInEntry(*root, name));
    });
    _selfApi.set_function("spawn", [](const sol::table& self, const std::string& documentPath,
                                      const LuaWidgetHandle& parent, sol::this_state state) -> sol::object {
        auto        behavior = behaviorOf(self);
        UIElement*  under    = behavior && behavior->runtime ? parent.get("spawn") : nullptr;
        if (!under) {
            return sol::make_object(state, sol::lua_nil);
        }
        GameUIHost& host = behavior->runtime->host;
        return makeLuaWidgetHandle(state, host, host.queueSpawn(documentPath, *under));
    });
}

LuaWidgetScripts::~LuaWidgetScripts()
{
    disconnectClicks(nullptr);
    std::vector<std::weak_ptr<LuaWidgetScriptBehavior>> all = std::move(_fresh);
    all.insert(all.end(), _started.begin(), _started.end());
    _started.clear();
    for (const auto& weak : all) {
        if (auto behavior = weak.lock(); behavior && behavior->runtime == this) {
            behavior->runtime = nullptr;
            scripting.destroy(behavior->instance);
            host.cancelTimersOf(behavior.get());
        }
    }
}

void LuaWidgetScripts::activate(UIElement& widget, const FUIBehaviorSpec& spec, const FUIBehaviorActivation& context)
{
    if (spec.type != "script.lua") {
        if (_warnedTypes.insert(spec.type).second) {
            YA_CORE_WARN("Game UI: no runtime for behaviour type '{}' (first seen on '{}'); ignored",
                         spec.type, widget._name);
        }
        return;
    }
    const auto script = spec.data.find("script");
    if (script == spec.data.end() || !script->is_string() || script->get_ref<const std::string&>().empty()) {
        YA_CORE_WARN("Game UI: script.lua on '{}' in entry '{}' has no \"script\" path", widget._name, context.entryId);
        return;
    }
    auto behavior                 = std::make_shared<LuaWidgetScriptBehavior>();
    behavior->instance.scriptPath = LuaScriptInstance::normalizeScriptPath(script->get<std::string>());
    behavior->runtime             = this;
    behavior->entryId             = std::string(context.entryId);
    behavior->entryRoot           = context.entryRoot.weak_from_this();
    behavior->entryZOrder         = context.entryRoot._zOrder;
    behavior->activationIndex     = _nextActivation++;
    if (!widget.addBehavior(behavior)) {
        YA_CORE_WARN("Game UI: '{}' in entry '{}' already has a script.lua; '{}' ignored",
                     widget._name, context.entryId, behavior->instance.scriptPath);
        return;
    }
    _fresh.push_back(behavior);
}

void LuaWidgetScripts::update()
{
    // Start the batch activated since the last step: across entries by zOrder,
    // then mount order; within an entry in tree pre-order (activation order).
    std::vector<std::shared_ptr<LuaWidgetScriptBehavior>> batch;
    for (const auto& weak : std::exchange(_fresh, {})) {
        if (auto behavior = weak.lock(); behavior && behavior->runtime == this && behavior->getOwner()) {
            batch.push_back(std::move(behavior));
        }
    }
    std::stable_sort(batch.begin(), batch.end(), [](const auto& a, const auto& b) {
        return a->entryZOrder != b->entryZOrder ? a->entryZOrder < b->entryZOrder
                                                : a->activationIndex < b->activationIndex;
    });
    for (const auto& behavior : batch) {
        _started.push_back(behavior);
        (void)scripting.load(behavior->instance, std::make_unique<FWidgetScriptHost>(behavior));
    }
    for (const auto& behavior : batch) {
        scripting.call(behavior->instance, ELuaScriptCallback::Init);
    }
    for (const auto& behavior : batch) {
        scripting.call(behavior->instance, ELuaScriptCallback::Start);
    }
    for (const auto& behavior : batch) {
        if (const UIElement* owner = behavior->getOwner()) {
            behavior->bWasVisible = owner->isVisibleInTree();
        }
    }

    // onShow / onHide: effective visibility can only have changed if the
    // tree's visibility revision moved. Read before the callbacks, so a change
    // they make is seen next update.
    const uint64_t visibilityRevision = host.getTree().getVisibilityRevision();
    if (visibilityRevision == _seenVisibilityRevision) {
        return;
    }
    _seenVisibilityRevision = visibilityRevision;
    std::erase_if(_started, [](const auto& weak) { return weak.expired(); });
    // Only this function appends to `_started`; a callback can end behaviours
    // (their weak refs expire) but not add any.
    for (size_t i = 0; i < _started.size(); ++i) {
        auto             behavior = _started[i].lock();
        const UIElement* owner    = behavior ? behavior->getOwner() : nullptr;
        if (!owner || behavior->runtime != this || !behavior->instance.bLoaded) {
            continue;
        }
        const bool bVisible = owner->isVisibleInTree();
        if (bVisible != behavior->bWasVisible) {
            behavior->bWasVisible = bVisible;
            (void)scripting.invoke(behavior->instance, bVisible ? "onShow" : "onHide");
        }
    }
}

void LuaWidgetScripts::release(LuaWidgetScriptBehavior& behavior)
{
    // Destroy first: an onDestroy that starts a timer makes one that finds its
    // instance gone and drops itself.
    scripting.destroy(behavior.instance);
    host.cancelTimersOf(&behavior);
    disconnectClicks(&behavior);
}

sol::object LuaWidgetScripts::connectClick(const LuaButtonHandle& button, const sol::object& target,
                                           const sol::protected_function& fn, sol::this_state state)
{
    sol::state_view lua(state);
    auto*           widget = button.as<UIButton>("onClick");
    if (!widget) {
        return sol::make_object(lua, sol::lua_nil);
    }
    if (!fn.valid() || fn.get_type() != sol::type::function) {
        YA_CORE_WARN("Lua button:onClick ('{}'): callback is not a function", widget->_name);
        return sol::make_object(lua, sol::lua_nil);
    }
    std::erase_if(_clickConnections, [](const FClickConnection& c) { return c.button.expired(); });

    // A listener for a widget script's `self` lives as long as that instance:
    // it stops on release and after a hot reload replaced `self`.
    std::shared_ptr<LuaWidgetScriptBehavior> owner = target.is<sol::table>() ? behaviorOf(target.as<sol::table>()) : nullptr;
    const std::weak_ptr<UIButton> weakButton = std::static_pointer_cast<UIButton>(widget->shared_from_this());
    const DelegateHandle          handle     = widget->onClicked.addLambda(
        [weakButton, weakOwner = std::weak_ptr(owner), bOwned = owner != nullptr, fn, target, ui = &host]() {
            if (bOwned) {
                auto live = weakOwner.lock();
                if (!live || !live->runtime || !live->instance.bLoaded ||
                    !sameTable(live->instance.self, target.as<sol::table>())) {
                    return;
                }
            }
            const std::shared_ptr<UIButton> clicked = weakButton.lock();
            if (!clicked) {
                return;
            }
            const sol::protected_function_result result = fn(target, makeLuaWidgetHandle(fn.lua_state(), *ui, clicked));
            if (!result.valid()) {
                const sol::error error = result;
                YA_CORE_ERROR("Lua button:onClick ('{}') error: {}", clicked->_name, error.what());
            }
        });
    _clickConnections.push_back({.button = weakButton, .handle = handle, .owner = owner.get()});
    return sol::make_object(lua, FLuaUIConnection{.button = weakButton, .handle = handle});
}

void LuaWidgetScripts::disconnectClicks(const LuaWidgetScriptBehavior* owner)
{
    std::erase_if(_clickConnections, [owner](const FClickConnection& connection) {
        if (owner && connection.owner != owner) {
            return false;
        }
        if (auto button = connection.button.lock()) {
            button->onClicked.remove(connection.handle);
        }
        return true;
    });
}

void LuaWidgetScripts::bindSelf(const std::shared_ptr<LuaWidgetScriptBehavior>& behavior, sol::table& self)
{
    // Once per `self`: the fields never change for a behaviour, and a hot
    // reload produces a new `self` that arrives here unbound.
    if (self.raw_get<sol::object>("__widgetScript").get_type() != sol::type::lua_nil) {
        return;
    }
    sol::state_view lua(self.lua_state());
    self.raw_set("__widgetScript", FWidgetScriptRef{.behavior = behavior});
    UIElement* owner = behavior->getOwner();
    self.raw_set("widget", makeLuaWidgetHandle(lua, host, owner ? owner->shared_from_this() : nullptr));
    self.raw_set("root", makeLuaWidgetHandle(lua, host, behavior->entryRoot.lock()));
    for (const auto& [key, value] : _selfApi) {
        self.raw_set(key, value);
    }
}

sol::object LuaWidgetScripts::scriptSelfOf(const UIElement& widget)
{
    const LuaWidgetScriptBehavior* script = widget.findBehavior<LuaWidgetScriptBehavior>();
    if (script && script->runtime && script->instance.bLoaded) {
        return script->instance.self;
    }
    return {};
}

} // namespace ya
