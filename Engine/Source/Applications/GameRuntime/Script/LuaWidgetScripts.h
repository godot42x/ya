#pragma once

// `script.lua` widget behaviours: a Lua script instance hosted by a widget.
//
// Lifecycle (UI scripts are event-driven, see plan D4):
//   activation (mount)      -> behaviour created, nothing runs
//   next UILogic            -> the batch loads, onInit all, then onStart all
//   visibility change       -> onShow / onHide in the next UILogic
//   self:setTickEnabled(b)  -> onUpdate from the tree tick while visible
//   self:after / self:every -> host-clock timers, visible or not
//   widget leaves the tree  -> timers cancelled, onDestroy
//
// Names written on `self` (reserved): widget, root, setTickEnabled, after,
// every, find.

#include "ECS/Systems/LuaScriptingSystem.h"

#include "GameRuntime/GUI/GameUI/IGameUIBehaviorRuntime.h"

#include "GUI/Widgets/UIBehavior.h"

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace ya
{

struct GameUIHost;
struct LuaWidgetScripts;

struct LuaWidgetScriptBehavior final : UIBehavior
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
    /// Activated, not yet started.
    std::vector<std::weak_ptr<LuaWidgetScriptBehavior>> _fresh;
    std::vector<std::weak_ptr<LuaWidgetScriptBehavior>> _started;
    /// Functions every widget script finds on `self`.
    sol::table                      _selfApi;
    uint64_t                        _nextActivation = 0;
    std::unordered_set<std::string> _warnedTypes;

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
};

} // namespace ya
