// `script.lua` widget behaviours (game-ui-script-framework S3): lifecycle,
// opt-in tick, host-clock timers, visibility callbacks, entry-scoped handles,
// action bubbling to UI then world scripts, deferred spawn / destroy.
// Each test runs a real Lua state with injected script sources; widgets are
// mounted through the scene-entry path like a running game.

#include "GameRuntime/GUI/GameUI/GameUIHost.h"
#include "GameRuntime/Script/GameplayLua.h"

#include "Core/Event.h"
#include "ECS/Systems/Components/LuaScriptComponent.h"
#include "ECS/Systems/LuaScriptingSystem.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/UIDocument.h"
#include "GUI/Widgets/UIDocumentStore.h"
#include "GUI/Widgets/UITypeIds.h"
#include "GUI/Widgets/UITypeRegistry.h"
#include "Scene/Core/Scene.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

namespace ya
{

namespace
{

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::UnorderedElementsAre;

constexpr const char* kProbe = R"(
local S = {}
local function log(self, what) table.insert(trace, (self.widget.name or "?") .. "." .. what) end
function S:onInit() log(self, "init") end
function S:onStart() log(self, "start") end
function S:onUpdate(dt) log(self, "update") end
function S:onShow() log(self, "show") end
function S:onHide() log(self, "hide") end
function S:onDestroy() log(self, "destroy") end
return S
)";

struct FUIScripts
{
    std::map<std::string, std::string> sources;
    LuaScriptingSystem                 lua;
    UIDocumentStore                    documents;
    /// After `lua`: the host's script runtime tears its instances down in it.
    GameUIHost                         host;
    Scene                              scene{"World"};

    FUIScripts()
    {
        lua.setRuntimeServices({
            .activeScene = [this]() -> Scene* { return &scene; },
            .readScript  = [this](const std::string& path, std::string& out) {
                auto it = sources.find(path);
                if (it == sources.end()) {
                    return false;
                }
                out = it->second;
                return true;
            },
        });
        lua.init();
        bindGameplayLua(lua, host);
        host.setDocumentStore(&documents);
        host.setPresentation(Rect2D{.pos = {0.0f, 0.0f}, .extent = {800.0f, 600.0f}}, {1.0f, 1.0f});
        lua.lua()["trace"] = lua.lua().create_table();
    }

    ~FUIScripts()
    {
        host.onSceneDeactivated(scene);
        host.setBehaviorRuntime(nullptr);
        lua.destroyAll();
    }

    static std::string scriptPath(const std::string& name) { return "Test/UI/" + name + ".lua"; }

    void addScript(const std::string& name, std::string source)
    {
        sources[LuaScriptInstance::normalizeScriptPath(scriptPath(name))] = std::move(source);
    }

    static UIElementRef widget(const char* typeId, const std::string& name, const std::string& script = {})
    {
        UIElementRef w = UITypeRegistry::instance().createInstance(typeId);
        w->_name       = name;
        if (!script.empty()) {
            w->_behaviorSpecs = {{.type = "script.lua", .data = {{"script", scriptPath(script)}}}};
        }
        return w;
    }

    static UIElementRef button(const std::string& name, const std::string& action)
    {
        UIElementRef b                     = widget(kTypeIdButton, name);
        static_cast<UIButton&>(*b)._action = action;
        return b;
    }

    /// A world script on its own entity, loaded and started.
    void addWorldScript(const std::string& name)
    {
        scene.createNode3D(name)->getEntity()->addComponent<LuaScriptComponent>()->addScript(scriptPath(name));
        lua.onUpdate(0.0f);
    }

    /// Activate a focused button from the keyboard (the same path as a click).
    void press(UIElement* target)
    {
        ASSERT_NE(target, nullptr);
        host.getTree().setFocus(target);
        KeyPressedEvent enter{};
        enter._keyCode = EKey::Enter;
        (void)host.getTree().dispatchEvent(enter, WidgetEventContext{});
    }

    /// Publish `root` as a document and add an autoMount entry for it.
    void addEntry(const std::string& entryId, const UIElementRef& root, int32_t zOrder = 0)
    {
        const std::string path = "Test/UI/" + entryId + ".yaui";
        documents.put(path, UIDocument::fromWidget(*root));
        SceneWidgetEntry entry;
        entry.entryId      = entryId;
        entry.documentPath = path;
        entry.zOrder       = zOrder;
        entry.autoMount    = true;
        scene.addWidgetEntry(entry);
    }

    void frame(float seconds = 1.0f / 60.0f) { host.update(FUIFrameClock{.gameDelta = seconds, .realDelta = seconds}); }

    void run(const std::string& code) { lua.lua().script(code); }

    template <typename T>
    T global(const char* name)
    {
        return lua.lua()[name].get<T>();
    }

    std::vector<std::string> takeTrace()
    {
        std::vector<std::string> out;
        sol::table               trace = lua.lua()["trace"];
        for (size_t i = 1; i <= trace.size(); ++i) {
            out.push_back(trace[i].get<std::string>());
        }
        lua.lua()["trace"] = lua.lua().create_table();
        return out;
    }

    UIElement* mounted(std::string_view entryId, std::string_view name)
    {
        UIElementRef root = host.findEntryRoot(entryId);
        return root ? host.findInEntry(*root, name).get() : nullptr;
    }
};

} // namespace

TEST(GameUIScriptTest, WidgetScriptReceivesLifecycleInOrder)
{
    FUIScripts ui;
    ui.addScript("Probe", kProbe);
    UIElementRef menu = FUIScripts::widget(kTypeIdCanvasPanel, "Menu", "Probe");
    menu->addDetachedChild(FUIScripts::widget(kTypeIdButton, "Start", "Probe"));
    ui.addEntry("Menu", menu, 10);
    ui.addEntry("HUD", FUIScripts::widget(kTypeIdText, "Score", "Probe"), 0);

    ui.host.onSceneActivated(ui.scene);
    EXPECT_THAT(ui.takeTrace(), IsEmpty()) << "mounting runs no script";

    // One batch: every onInit before any onStart; entries by zOrder, then tree order.
    ui.frame();
    EXPECT_THAT(ui.takeTrace(), ElementsAre("Score.init", "Menu.init", "Start.init",
                                            "Score.start", "Menu.start", "Start.start"));

    // Event-driven: no onUpdate unless a script asks for ticks.
    for (int i = 0; i < 3; ++i) {
        ui.frame();
    }
    EXPECT_THAT(ui.takeTrace(), IsEmpty());

    ui.host.onSceneDeactivated(ui.scene);
    EXPECT_THAT(ui.takeTrace(), UnorderedElementsAre("Score.destroy", "Menu.destroy", "Start.destroy"));
}

TEST(GameUIScriptTest, WidgetScriptTicksOnlyAfterEnablingAndWhileVisible)
{
    FUIScripts ui;
    ui.addScript("Ticker", R"(
local S = {}
function S:onInit() probe = self; updates = 0 end
function S:onUpdate(dt) updates = updates + 1 end
return S
)");
    ui.addEntry("HUD", FUIScripts::widget(kTypeIdBorder, "HUD", "Ticker"));
    ui.host.onSceneActivated(ui.scene);

    ui.frame();
    ui.frame();
    EXPECT_EQ(ui.global<int>("updates"), 0);

    ui.run("probe:setTickEnabled(true)");
    ui.frame();
    ui.frame();
    EXPECT_EQ(ui.global<int>("updates"), 2);

    ui.run("probe.widget.visible = false");
    ui.frame();
    ui.frame();
    EXPECT_EQ(ui.global<int>("updates"), 2) << "a hidden widget is not ticked";

    ui.run("probe.widget.visible = true");
    ui.frame();
    EXPECT_EQ(ui.global<int>("updates"), 3);

    ui.run("probe:setTickEnabled(false)");
    ui.frame();
    EXPECT_EQ(ui.global<int>("updates"), 3);
}

TEST(GameUIScriptTest, WidgetTimerFiresWhileHiddenAndStopsWithItsScript)
{
    FUIScripts ui;
    ui.addScript("Timers", R"(
local S = {}
function S:onInit()
    once, ticks, cancelled = 0, 0, 0
    self:after(0.5, function() once = once + 1 end)
    self:every(0.25, function() ticks = ticks + 1 end)
    handle = self:every(0.25, function() cancelled = cancelled + 1 end)
end
return S
)");
    UIElementRef popup = FUIScripts::widget(kTypeIdBorder, "Popup", "Timers");
    popup->setVisibility(EWidgetVisibility::Hidden);
    ui.addEntry("Popup", popup);
    ui.host.onSceneActivated(ui.scene);

    // 0.125 s steps: the host clock reads 0.25 after frame 2, 0.5 after frame 4.
    for (int i = 0; i < 4; ++i) {
        ui.frame(0.125f);
    }
    EXPECT_EQ(ui.global<int>("once"), 1);
    EXPECT_EQ(ui.global<int>("ticks"), 2);
    EXPECT_EQ(ui.global<int>("cancelled"), 2);

    ui.run("handle:cancel()");
    ui.frame(0.125f);
    ui.frame(0.125f);
    EXPECT_EQ(ui.global<int>("ticks"), 3);
    EXPECT_EQ(ui.global<int>("cancelled"), 2);
    EXPECT_EQ(ui.global<int>("once"), 1) << "after() fires once";

    ui.host.onSceneDeactivated(ui.scene);
    for (int i = 0; i < 4; ++i) {
        ui.frame(0.125f);
    }
    EXPECT_EQ(ui.global<int>("ticks"), 3) << "timers stop with the script";
}

TEST(GameUIScriptTest, WidgetTimerFollowsHostClock)
{
    FUIScripts ui;
    ui.addScript("Delay", R"(
local S = {}
function S:onInit() fired = false; self:after(0.25, function() fired = true end) end
return S
)");
    ui.addEntry("HUD", FUIScripts::widget(kTypeIdBorder, "HUD", "Delay"));
    ui.host.setUpdateClock(EUIUpdateClock::GameTime);
    ui.host.onSceneActivated(ui.scene);

    for (int i = 0; i < 3; ++i) {
        ui.host.update(FUIFrameClock{.gameDelta = 0.0f, .realDelta = 1.0f});
    }
    EXPECT_FALSE(ui.global<bool>("fired")) << "a paused game clock holds the timer";

    ui.host.update(FUIFrameClock{.gameDelta = 0.25f, .realDelta = 0.0f});
    EXPECT_TRUE(ui.global<bool>("fired"));
}

TEST(GameUIScriptTest, WidgetScriptSeesShowAndHide)
{
    FUIScripts ui;
    ui.addScript("Probe", kProbe);
    UIElementRef pause = FUIScripts::widget(kTypeIdCanvasPanel, "Pause", "Probe");
    pause->addDetachedChild(FUIScripts::widget(kTypeIdButton, "Resume", "Probe"));
    pause->setVisibility(EWidgetVisibility::Hidden);
    ui.addEntry("Pause", pause);
    ui.host.onSceneActivated(ui.scene);
    ui.frame();
    (void)ui.takeTrace();

    ui.host.findEntryRoot("Pause")->setVisibility(EWidgetVisibility::Visible);
    ui.frame();
    EXPECT_THAT(ui.takeTrace(), ElementsAre("Pause.show", "Resume.show"));

    ui.frame();
    EXPECT_THAT(ui.takeTrace(), IsEmpty());

    ui.host.findEntryRoot("Pause")->setVisibility(EWidgetVisibility::Collapsed);
    ui.frame();
    EXPECT_THAT(ui.takeTrace(), ElementsAre("Pause.hide", "Resume.hide"));
}

TEST(GameUIScriptTest, WidgetScriptSeesShowWhenMovedOutOfHiddenParent)
{
    FUIScripts   ui;
    ui.addScript("Probe", kProbe);
    UIElementRef root   = FUIScripts::widget(kTypeIdCanvasPanel, "Root");
    UIElementRef drawer = FUIScripts::widget(kTypeIdCanvasPanel, "Drawer");
    drawer->setVisibility(EWidgetVisibility::Hidden);
    drawer->addDetachedChild(FUIScripts::widget(kTypeIdButton, "Item", "Probe"));
    root->addDetachedChild(drawer);
    ui.addEntry("Inventory", root);
    ui.host.onSceneActivated(ui.scene);
    ui.frame();
    (void)ui.takeTrace();

    UIElement* item = ui.mounted("Inventory", "Item");
    ASSERT_NE(item, nullptr);
    ui.host.getTree().reparent(*ui.host.findEntryRoot("Inventory"), item->shared_from_this());
    ui.frame();
    EXPECT_THAT(ui.takeTrace(), ElementsAre("Item.show"));
}

TEST(GameUIScriptTest, FindIsScopedToOwningEntry)
{
    FUIScripts ui;
    ui.addScript("Finder", R"(
local S = {}
function S:onInit()
    rootName = self.root.name
    missing = self:find("Missing") == nil
    self:find("Label").text = "from A"
end
return S
)");
    UIElementRef a = FUIScripts::widget(kTypeIdCanvasPanel, "A", "Finder");
    a->addDetachedChild(FUIScripts::widget(kTypeIdText, "Label"));
    UIElementRef b = FUIScripts::widget(kTypeIdCanvasPanel, "B");
    b->addDetachedChild(FUIScripts::widget(kTypeIdText, "Label"));
    ui.addEntry("B", b, 0);
    ui.addEntry("A", a, 1);
    ui.host.onSceneActivated(ui.scene);
    ui.frame();

    EXPECT_EQ(ui.global<std::string>("rootName"), "A");
    EXPECT_TRUE(ui.global<bool>("missing"));
    auto* labelA = dynamic_cast<UIText*>(ui.mounted("A", "Label"));
    auto* labelB = dynamic_cast<UIText*>(ui.mounted("B", "Label"));
    ASSERT_NE(labelA, nullptr);
    ASSERT_NE(labelB, nullptr);
    EXPECT_EQ(labelA->getText(), "from A");
    EXPECT_NE(labelB->getText(), "from A");
}

TEST(GameUIScriptTest, DanglingHandleIsNoOp)
{
    FUIScripts ui;
    ui.addScript("Keeper", R"(
local S = {}
function S:onInit() label = self:find("Label") end
return S
)");
    UIElementRef hud = FUIScripts::widget(kTypeIdCanvasPanel, "HUD", "Keeper");
    hud->addDetachedChild(FUIScripts::widget(kTypeIdText, "Label"));
    ui.addEntry("HUD", hud);
    ui.host.onSceneActivated(ui.scene);
    ui.frame();
    ui.run("wasValid = label.valid");
    EXPECT_TRUE(ui.global<bool>("wasValid"));

    UIElement* label = ui.mounted("HUD", "Label");
    ASSERT_NE(label, nullptr);
    ui.host.getTree().detach(*label);

    ui.run(R"(
label.text = "ignored"
text = label.text
isValid = label.valid
label.visible = false
)");
    EXPECT_FALSE(ui.global<bool>("isValid"));
    EXPECT_FALSE(ui.lua.lua()["text"].valid()) << "a dangling read is nil";
}

TEST(GameUIScriptTest, UiGetReturnsTheEntryScriptOrItsRoot)
{
    FUIScripts ui;
    ui.addScript("HUD", R"(
local S = {}
function S:setScore(n) self:find("Score").text = "Score: " .. n end
return S
)");
    UIElementRef hud = FUIScripts::widget(kTypeIdCanvasPanel, "HUD", "HUD");
    hud->addDetachedChild(FUIScripts::widget(kTypeIdText, "Score"));
    ui.addEntry("HUD", hud);
    ui.addEntry("Plain", FUIScripts::widget(kTypeIdBorder, "PlainRoot"));
    ui.host.onSceneActivated(ui.scene);
    ui.frame();

    ui.run(R"(
ui.get("HUD"):setScore(7)
plainName = ui.get("Plain").name
missing = ui.get("Missing") == nil
)");
    auto* score = dynamic_cast<UIText*>(ui.mounted("HUD", "Score"));
    ASSERT_NE(score, nullptr);
    EXPECT_EQ(score->getText(), "Score: 7");
    EXPECT_EQ(ui.global<std::string>("plainName"), "PlainRoot");
    EXPECT_TRUE(ui.global<bool>("missing"));
}

TEST(GameUIScriptTest, RemovingTheRuntimeDestroysRunningScripts)
{
    FUIScripts ui;
    ui.addScript("Probe", kProbe);
    ui.addEntry("HUD", FUIScripts::widget(kTypeIdBorder, "HUD", "Probe"));
    ui.host.onSceneActivated(ui.scene);
    ui.frame();
    (void)ui.takeTrace();

    ui.host.setBehaviorRuntime(nullptr);
    EXPECT_THAT(ui.takeTrace(), ElementsAre("HUD.destroy"));
    EXPECT_EQ(ui.lua.liveCount(), 0u);

    // Remounted without a runtime: the spec stays inert.
    ui.frame();
    EXPECT_THAT(ui.takeTrace(), IsEmpty());
}

TEST(GameUIScriptTest, ButtonActionBubblesToNearestScriptThenWorld)
{
    FUIScripts ui;
    ui.addScript("Relay", R"(
local S = {}
function S:onAction(name, source)
    table.insert(trace, self.widget.name .. ":" .. name .. ":" .. source.name)
    return self.widget.name == "Panel" and name == "local"
end
return S
)");
    ui.addScript("World", R"(
local S = {}
function S:onUiAction(name, source)
    table.insert(trace, "world:" .. name .. ":" .. source.name)
    return name ~= "old"
end
return S
)");
    UIElementRef panel = FUIScripts::widget(kTypeIdCanvasPanel, "Panel", "Relay");
    UIElementRef row   = FUIScripts::widget(kTypeIdCanvasPanel, "Row", "Relay");
    row->addDetachedChild(FUIScripts::button("Local", "local"));
    row->addDetachedChild(FUIScripts::button("Restart", "restart"));
    row->addDetachedChild(FUIScripts::button("Old", "old"));
    panel->addDetachedChild(row);
    ui.addEntry("Menu", panel);
    ui.host.onSceneActivated(ui.scene);
    ui.addWorldScript("World");
    ui.frame();
    ui.run(R"(onUiAction = function(name) table.insert(trace, "legacy:" .. name) end)");

    ui.press(ui.mounted("Menu", "Local"));
    EXPECT_THAT(ui.takeTrace(), ElementsAre("Row:local:Local", "Panel:local:Local"));

    ui.press(ui.mounted("Menu", "Restart"));
    EXPECT_THAT(ui.takeTrace(), ElementsAre("Row:restart:Restart", "Panel:restart:Restart", "world:restart:Restart"));

    ui.press(ui.mounted("Menu", "Old"));
    EXPECT_THAT(ui.takeTrace(), ElementsAre("Row:old:Old", "Panel:old:Old", "world:old:Old", "legacy:old"));
}

TEST(GameUIScriptTest, SpawnedButtonRoutesActionWithoutRemount)
{
    FUIScripts ui;
    ui.addScript("Shop", R"(
local S = {}
function S:onInit()
    item = self:spawn("Test/UI/Item.yaui", self.widget)
    item:find("Buy").action = "buy.sword"
    pendingValid = item.valid
end
function S:onAction(name, source)
    table.insert(trace, "shop:" .. name .. ":" .. source.name)
    return true
end
return S
)");
    ui.addScript("Item", R"(
local S = {}
function S:onInit() table.insert(trace, "item.init:" .. self.root.name) end
return S
)");
    UIElementRef item = FUIScripts::widget(kTypeIdCanvasPanel, "Item", "Item");
    item->addDetachedChild(FUIScripts::button("Buy", "buy"));
    ui.documents.put("Test/UI/Item.yaui", UIDocument::fromWidget(*item));
    ui.addEntry("Shop", FUIScripts::widget(kTypeIdCanvasPanel, "Shop", "Shop"));
    ui.host.onSceneActivated(ui.scene);

    ui.frame();
    EXPECT_TRUE(ui.global<bool>("pendingValid")) << "a pending spawn can be configured";
    EXPECT_EQ(ui.mounted("Shop", "Buy"), nullptr) << "a spawn waits for StructuralFlush";

    ui.host.flushStructuralChanges();
    UIElement* buy = ui.mounted("Shop", "Buy");
    ASSERT_NE(buy, nullptr);
    ui.frame();
    EXPECT_THAT(ui.takeTrace(), ElementsAre("item.init:Shop")) << "spawned scripts join the parent's entry";

    ui.press(buy);
    EXPECT_THAT(ui.takeTrace(), ElementsAre("shop:buy.sword:Buy"));
}

TEST(GameUIScriptTest, DestroyWaitsForStructuralFlushAndDropsSpawnsUnderIt)
{
    FUIScripts ui;
    ui.addScript("Probe", kProbe);
    ui.addScript("Spawner", R"(
local S = {}
function S:onInit() spawner = self end
return S
)");
    UIElementRef hud = FUIScripts::widget(kTypeIdCanvasPanel, "HUD", "Spawner");
    hud->addDetachedChild(FUIScripts::widget(kTypeIdBorder, "Toast", "Probe"));
    hud->addDetachedChild(FUIScripts::widget(kTypeIdCanvasPanel, "Slot"));
    ui.documents.put("Test/UI/Box.yaui", UIDocument::fromWidget(*FUIScripts::widget(kTypeIdBorder, "Box")));
    UIElementRef crate = FUIScripts::widget(kTypeIdCanvasPanel, "Crate");
    crate->addDetachedChild(FUIScripts::widget(kTypeIdBorder, "Lid"));
    ui.documents.put("Test/UI/Crate.yaui", UIDocument::fromWidget(*crate));
    ui.addEntry("HUD", hud);
    ui.host.onSceneActivated(ui.scene);
    ui.frame();
    (void)ui.takeTrace();

    ui.run(R"(
toast = spawner:find("Toast")
toast:destroy()
box = spawner:spawn("Test/UI/Box.yaui", spawner:find("Slot"))
spawner:find("Slot"):destroy()
crate = spawner:spawn("Test/UI/Crate.yaui", spawner.widget)
crate:find("Lid"):destroy()
)");
    EXPECT_NE(ui.mounted("HUD", "Toast"), nullptr);
    EXPECT_THAT(ui.takeTrace(), IsEmpty());

    ui.host.flushStructuralChanges();
    EXPECT_THAT(ui.takeTrace(), ElementsAre("Toast.destroy"));
    EXPECT_EQ(ui.mounted("HUD", "Toast"), nullptr);
    EXPECT_EQ(ui.mounted("HUD", "Slot"), nullptr);
    EXPECT_EQ(ui.mounted("HUD", "Box"), nullptr);
    EXPECT_NE(ui.mounted("HUD", "Crate"), nullptr);
    EXPECT_EQ(ui.mounted("HUD", "Lid"), nullptr) << "destroying inside a pending spawn takes effect";
    ui.run("gone = not toast.valid and not box.valid");
    EXPECT_TRUE(ui.global<bool>("gone"));
}

TEST(GameUIScriptTest, AddToWorldWidgetRunsScriptsAndFinds)
{
    FUIScripts ui;
    ui.addScript("Probe", kProbe);
    ui.host.onSceneActivated(ui.scene);

    UIElementRef toast = FUIScripts::widget(kTypeIdCanvasPanel, "Toast", "Probe");
    toast->addDetachedChild(FUIScripts::widget(kTypeIdText, "Message"));
    ASSERT_TRUE(ui.host.addToWorld(ui.scene, toast).valid());
    ui.frame();
    EXPECT_THAT(ui.takeTrace(), ElementsAre("Toast.init", "Toast.start"));

    ui.run(R"(ui.get("Toast"):find("Message").text = "saved")");
    auto* message = dynamic_cast<UIText*>(ui.mounted("Toast", "Message"));
    ASSERT_NE(message, nullptr);
    EXPECT_EQ(message->getText(), "saved");
}

} // namespace ya
