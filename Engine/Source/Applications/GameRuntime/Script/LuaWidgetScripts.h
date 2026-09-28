#pragma once

// `script.lua` widget behaviours: a Lua script instance hosted by a widget.
//
// Lifecycle (UI scripts are event-driven, see plan D4):
//   activation (mount)      -> behaviour created, nothing runs
//   next UILogic            -> the batch loads, onInit all, then onStart all
//   visibility change       -> onShow / onHide in the next UILogic
//   self:setTickEnabled(b)  -> onUpdate from the tree tick while visible
//   self:after / self:every -> host-clock timers, visible or not
//   button:onClick(self, fn) -> fn(self, button) on each click
//   widget leaves the tree  -> timers cancelled, click listeners it made
//                              disconnected, onDestroy
//
// Any script (UI or world) listens to a button directly through its handle;
// the button names no handler. A listener whose target is not a widget
// script's `self` lives until the button goes, disconnect(), or this runtime
// is destroyed (always before the Lua state).
//
// Names written on `self` (reserved): widget, root, setTickEnabled, after,
// every, find, spawn.

#include "ECS/Systems/LuaScriptingSystem.h"

#include "GameRuntime/GUI/GameUI/IGameUIBehaviorRuntime.h"

#include "Core/Delegate.h"
#include "GUI/Widgets/UIBehavior.h"

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace ya
{

struct GameUIHost;
struct LuaButtonHandle;
struct LuaWidgetScripts;
struct UIButton;

struct LuaWidgetScriptBehavior final : UIBehaviorWith<LuaWidgetScriptBehavior, IUITickable>
{
    LuaScriptInstance        instance;
    /// Null once the runtime is gone; the behaviour is inert from then on.
    LuaWidgetScripts*        runtime = nullptr;
    std::string              entryId;
    std::weak_ptr<UIElement> entryRoot;
    int32_t                  entryZOrder     = 0;
    uint64_t                 activationIndex = 0;
    bool                     bTickEnabled    = false;
    bool                     bWasVisible     = false;

    ~LuaWidgetScriptBehavior() override;

    [[nodiscard]] bool wantsTick() const override { return bTickEnabled && runtime && instance.bLoaded; }
    void               tick(UIElement& owner, float deltaSeconds) override;
    void               onDetached(UIElement& owner) override;
};

struct LuaWidgetScripts final : IGameUIBehaviorRuntime
{
    LuaScriptingSystem& scripting;
    GameUIHost&         host;

  private:
    struct FClickConnection
    {
        std::weak_ptr<UIButton> button;
        DelegateHandle          handle = INVALID_HANDLE;
        /// The widget script whose `self` is the target (disconnected when it
        /// is released), or null.
        const LuaWidgetScriptBehavior* owner = nullptr;
    };

    /// Activated, not yet started.
    std::vector<std::weak_ptr<LuaWidgetScriptBehavior>> _fresh;
    std::vector<std::weak_ptr<LuaWidgetScriptBehavior>> _started;
    /// The tree visibility revision `_started` was last checked against.
    uint64_t _seenVisibilityRevision = 0;
    /// Functions every widget script finds on `self`.
    sol::table                      _selfApi;
    uint64_t                        _nextActivation = 0;
    std::unordered_set<std::string> _warnedTypes;
    std::vector<FClickConnection>   _clickConnections;

  public:
    LuaWidgetScripts(LuaScriptingSystem& inScripting, GameUIHost& inHost);
    ~LuaWidgetScripts() override;
    LuaWidgetScripts(const LuaWidgetScripts&)            = delete;
    LuaWidgetScripts& operator=(const LuaWidgetScripts&) = delete;

    void activate(UIElement& widget, const FUIBehaviorSpec& spec, const FUIBehaviorActivation& context) override;
    void update() override;

    /// Cancel `behavior`'s timers and destroy its instance (onDestroy).
    void release(LuaWidgetScriptBehavior& behavior);
    /// Put the host fields and the self API on a freshly bound `self`.
    void bindSelf(const std::shared_ptr<LuaWidgetScriptBehavior>& behavior, sol::table& self);
    /// The `self` of the first loaded script on `widget`, or nil.
    [[nodiscard]] static sol::object scriptSelfOf(const UIElement& widget);

  private:
    /// `button:onClick(target, fn)`: a connection with `disconnect()`, or nil.
    sol::object connectClick(const LuaButtonHandle& button, const sol::object& target, const sol::protected_function& fn,
                             sol::this_state state);
    /// Disconnect the listeners made for `owner`'s `self`; null: all of them.
    void disconnectClicks(const LuaWidgetScriptBehavior* owner);
};

} // namespace ya
