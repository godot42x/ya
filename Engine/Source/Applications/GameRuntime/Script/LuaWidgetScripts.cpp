#include "LuaWidgetScripts.h"

#include "LuaWidgetHandle.h"
#include "WidgetScriptRefs.h"

#include "Core/Log.h"
#include "Core/Scripting/ScriptBindings.h"
#include "GameRuntime/GUI/GameUI/GameUIHost.h"
#include "GUI/Widgets/UIElement.h"
#include "Script/Lua/ScriptInstanceOps.h"

#include <algorithm>
#include <optional>
#include <utility>

namespace ya
{
namespace
{

struct FWidgetScriptHost final : IScriptInstanceHost
{
    std::weak_ptr<LuaWidgetScriptBehavior> behavior;

    explicit FWidgetScriptHost(std::weak_ptr<LuaWidgetScriptBehavior> inBehavior) : behavior(std::move(inBehavior)) {}

    ScriptInstance* resolve(uint64_t instanceId) override
    {
        const std::shared_ptr<LuaWidgetScriptBehavior> live = behavior.lock();
        return live && live->instance.runtimeId == instanceId ? &live->instance : nullptr;
    }

    void contributeSelf(ScriptSelfBuilder& out) override;
};

script::ScriptValue widgetRefValue(GameUIHost& host, const UIElementRef& widget)
{
    const script::ScriptRef ref = widgetScriptRef(host, widget);
    return ref ? script::scriptMake(ref) : script::ScriptValue{};
}

script::ScriptValue addScriptTimer(const std::shared_ptr<LuaWidgetScriptBehavior>& behavior, script::ScriptArgs args, bool bRepeat)
{
    if (!behavior || !behavior->runtime) {
        return {};
    }
    if (args.size() < 2) {
        YA_CORE_WARN("Lua widget timer ({}): callback is not a function", behavior->instance.scriptPath);
        return {};
    }
    const auto* fn = script::scriptIf<script::ScriptFnHandle>(args[1]);
    if (!fn || fn->id == 0 || !retainInstanceFunction(behavior->instance.runtimeId, *fn)) {
        YA_CORE_WARN("Lua widget timer ({}): callback is not a function", behavior->instance.scriptPath);
        return {};
    }
    const float delay = static_cast<float>(script::scriptToNumber(args[0]));
    const float interval = bRepeat ? delay : 0.0f;
    GameUIHost& host = behavior->runtime->host;
    const script::ScriptFnHandle held = *fn;
    const script::ScriptSelfHandle self{.runtimeId = behavior->instance.runtimeId};
    const std::weak_ptr<LuaWidgetScriptBehavior> weak = behavior;
    // The function is retained on this instance. Replacing or destroying the
    // self releases it, and the timer stops instead of calling into the new table.
    const EUIUpdateClock clock = host.clockForEntry(behavior->entryId);
    const uint64_t id = host.addTimer(behavior.get(), delay, interval, [weak, held, self]() -> bool {
        const std::shared_ptr<LuaWidgetScriptBehavior> live = weak.lock();
        if (!live || !live->runtime || !live->instance.bLoaded || !scriptFunctionAlive(held)) {
            return false;
        }
        if (script::IScriptCallbacks* callbacks = script::scriptCallbacks()) {
            callbacks->invoke(held, self, {});
        }
        return true;
    }, clock);
    return script::scriptMake(makeWidgetTimerRef(host, id));
}

void fillWidgetSelf(const std::shared_ptr<LuaWidgetScriptBehavior>& behavior, ScriptSelfBuilder& out)
{
    LuaWidgetScripts& runtime = *behavior->runtime;
    const std::weak_ptr<LuaWidgetScriptBehavior> weak = behavior;
    // Detach clears the owner before onDestroy. Leave the fields already on
    // this self so `self.widget.name` still resolves for that callback.
    if (UIElement* owner = behavior->getOwner()) {
        out.field("widget", widgetRefValue(runtime.host, owner->shared_from_this()));
    }
    if (const UIElementRef root = behavior->entryRoot.lock()) {
        out.field("root", widgetRefValue(runtime.host, root));
    }
    out.method("setTickEnabled", [weak](script::ScriptArgs args) -> script::ScriptValue {
        if (const std::shared_ptr<LuaWidgetScriptBehavior> live = weak.lock(); live && !args.empty()) {
            live->setTickEnabled(script::scriptToBool(args[0]));
        }
        return {};
    });
    out.method("after", [weak](script::ScriptArgs args) -> script::ScriptValue {
        return addScriptTimer(weak.lock(), args, false);
    });
    out.method("every", [weak](script::ScriptArgs args) -> script::ScriptValue {
        return addScriptTimer(weak.lock(), args, true);
    });
    out.method("find", [weak](script::ScriptArgs args) -> script::ScriptValue {
        const std::shared_ptr<LuaWidgetScriptBehavior> live = weak.lock();
        const UIElementRef root = live ? live->entryRoot.lock() : nullptr;
        if (!live || !live->runtime || !root || args.empty()) {
            return {};
        }
        return widgetRefValue(live->runtime->host, live->runtime->host.findInEntry(*root, script::scriptToString(args[0])));
    });
    out.method("spawn", [weak](script::ScriptArgs args) -> script::ScriptValue {
        const std::shared_ptr<LuaWidgetScriptBehavior> live = weak.lock();
        if (!live || !live->runtime || args.size() < 2) {
            return {};
        }
        const auto* parent = script::scriptIf<script::ScriptRef>(args[1]);
        UIElement* under = parent ? resolveWidget(parent->a) : nullptr;
        if (!under || !widgetLive(under)) {
            if (parent) {
                warnWidgetGone(parent->a, "spawn");
            }
            return {};
        }
        GameUIHost& host = live->runtime->host;
        return widgetRefValue(host, host.queueSpawn(script::scriptToString(args[0]), *under));
    });
}

void FWidgetScriptHost::contributeSelf(ScriptSelfBuilder& out)
{
    if (const std::shared_ptr<LuaWidgetScriptBehavior> live = behavior.lock(); live && live->runtime) {
        fillWidgetSelf(live, out);
    }
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
    if (runtime && instance.bLoaded) {
        (void)callScriptInstance(runtime->scripting, instance, EScriptCallback::Update, deltaSeconds);
    }
}

void LuaWidgetScriptBehavior::onDetached(UIElement& owner)
{
    UIBehavior::onDetached(owner);
    if (runtime) {
        runtime->release(*this);
    }
}

LuaWidgetScripts::LuaWidgetScripts(IWorldScriptRuntime& inScripting, GameUIHost& inHost)
    : scripting(inScripting), host(inHost)
{
    (void)widgetRefKind();
}

LuaWidgetScripts::~LuaWidgetScripts()
{
    std::vector<std::weak_ptr<LuaWidgetScriptBehavior>> all = std::move(_fresh);
    all.insert(all.end(), _started.begin(), _started.end());
    _started.clear();
    for (const auto& weak : all) {
        if (auto behavior = weak.lock(); behavior && behavior->runtime == this) {
            behavior->runtime = nullptr;
            destroyScriptInstance(scripting, behavior->instance);
            host.cancelTimersOf(behavior.get());
            behavior->setTickEnabled(false);
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
    behavior->instance.scriptPath = ScriptInstance::normalizeScriptPath(script->get<std::string>());
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
        (void)loadScriptInstance(scripting, behavior->instance, std::make_unique<FWidgetScriptHost>(behavior));
    }
    for (const auto& behavior : batch) {
        (void)callScriptInstance(scripting, behavior->instance, EScriptCallback::Init);
    }
    for (const auto& behavior : batch) {
        (void)callScriptInstance(scripting, behavior->instance, EScriptCallback::Start);
    }
    for (const auto& behavior : batch) {
        if (const UIElement* owner = behavior->getOwner()) {
            behavior->bWasVisible = owner->isVisibleInTree();
        }
    }

    const uint64_t visibilityRevision = host.getTree().getVisibilityRevision();
    if (visibilityRevision == _seenVisibilityRevision) {
        return;
    }
    _seenVisibilityRevision = visibilityRevision;
    std::erase_if(_started, [](const auto& weak) { return weak.expired(); });
    for (size_t i = 0; i < _started.size(); ++i) {
        auto             behavior = _started[i].lock();
        const UIElement* owner    = behavior ? behavior->getOwner() : nullptr;
        if (!owner || behavior->runtime != this || !behavior->instance.bLoaded) {
            continue;
        }
        const bool bVisible = owner->isVisibleInTree();
        if (bVisible != behavior->bWasVisible) {
            behavior->bWasVisible = bVisible;
            (void)callScriptInstanceNamed(scripting, behavior->instance, bVisible ? "onShow" : "onHide", {}, true);
        }
    }
}

bool LuaWidgetScripts::callNamed(std::string_view entryId, const std::string& name)
{
    std::vector<std::shared_ptr<LuaWidgetScriptBehavior>> scripts;
    for (const auto& weak : _started) {
        if (auto behavior = weak.lock();
            behavior && behavior->entryId == entryId && behavior->instance.bLoaded) {
            scripts.push_back(std::move(behavior));
        }
    }
    for (const auto& behavior : scripts) {
        const script::ScriptValue result = callScriptInstanceNamed(scripting, behavior->instance, name, {}, true);
        if (const std::optional<bool> flag = result.toBool(); flag && *flag) {
            return true;
        }
    }
    return false;
}

void LuaWidgetScripts::release(LuaWidgetScriptBehavior& behavior)
{
    // Destroy first: an onDestroy that starts a timer makes one that finds its
    // instance gone and drops itself.
    destroyScriptInstance(scripting, behavior.instance);
    host.cancelTimersOf(&behavior);
    behavior.setTickEnabled(false);
}

script::ScriptSelfHandle LuaWidgetScripts::scriptSelfOf(const UIElement& widget)
{
    const LuaWidgetScriptBehavior* script = widget.findBehavior<LuaWidgetScriptBehavior>();
    if (script && script->runtime && script->instance.bLoaded && script->instance.runtimeId != 0) {
        return script::ScriptSelfHandle{.runtimeId = script->instance.runtimeId};
    }
    return {};
}

} // namespace ya
