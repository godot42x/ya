#pragma once

// `script.lua` widget behaviours: a script instance hosted by a widget.
//
// Lifecycle (UI scripts are event-driven):
//   activation (mount)      -> behaviour created, nothing runs
//   next UILogic            -> the batch loads, onInit all, then onStart all
//   visibility change       -> onShow / onHide in the next UILogic
//   self:setTickEnabled(b)  -> onUpdate from the tree tick while visible
//   self:after / self:every -> host-clock timers, visible or not
//   widget leaves the tree  -> timers cancelled, onDestroy
//
// A script listens to a control through its handle, `btn.onClicked:add(self, fn)`.
// The listener ends with that script instance.
//
// Names written on `self` (reserved): widget, root, setTickEnabled, after,
// every, find, spawn.

#include "Core/Scripting/ScriptValue.h"
#include "ECS/Systems/ScriptInstance.h"
#include "ECS/Systems/WorldScriptRuntime.h"
#include "GUI/Widgets/UIBehavior.h"
#include "GameRuntime/GUI/GameUI/IGameUIBehaviorRuntime.h"

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace ya
{

struct GameUIHost;
struct LuaWidgetScripts;

struct LuaWidgetScriptBehavior final : UIBehaviorWith<LuaWidgetScriptBehavior, IUITickable>
{
    ScriptInstance               instance;
    /// Null once the runtime is gone; the behaviour is inert from then on.
    LuaWidgetScripts*            runtime = nullptr;
    std::string                  entryId;
    std::weak_ptr<UIElement>     entryRoot;
    int32_t                      entryZOrder     = 0;
    uint64_t                     activationIndex = 0;
    bool                         bWasVisible     = false;

    /// Registered for frames only after the script asks (self:setTickEnabled).
    LuaWidgetScriptBehavior() { setTickEnabled(false); }
    ~LuaWidgetScriptBehavior() override;

    void tick(UIElement& owner, float deltaSeconds) override;
    void onDetached(UIElement& owner) override;
};

struct LuaWidgetScripts final : IGameUIBehaviorRuntime
{
    IWorldScriptRuntime& scripting;
    GameUIHost&          host;

  private:
    /// Activated, not yet started.
    std::vector<std::weak_ptr<LuaWidgetScriptBehavior>> _fresh;
    std::vector<std::weak_ptr<LuaWidgetScriptBehavior>> _started;
    /// The tree visibility revision `_started` was last checked against.
    uint64_t                        _seenVisibilityRevision = 0;
    uint64_t                        _nextActivation         = 0;
    std::unordered_set<std::string> _warnedTypes;

  public:
    LuaWidgetScripts(IWorldScriptRuntime& inScripting, GameUIHost& inHost);
    ~LuaWidgetScripts() override;
    LuaWidgetScripts(const LuaWidgetScripts&)            = delete;
    LuaWidgetScripts& operator=(const LuaWidgetScripts&) = delete;

    void activate(UIElement& widget, const FUIBehaviorSpec& spec, const FUIBehaviorActivation& context) override;
    void update() override;
    bool callNamed(std::string_view entryId, const std::string& name) override;

    /// Cancel `behavior`'s timers and destroy its instance (onDestroy).
    void release(LuaWidgetScriptBehavior& behavior);
    /// The loaded script on `widget`, or an empty handle.
    [[nodiscard]] static script::ScriptSelfHandle scriptSelfOf(const UIElement& widget);
};

} // namespace ya
