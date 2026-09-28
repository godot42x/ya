// Frame-order contract (game-ui-script-framework F0): world-script order and
// init/start/update passes, what game pause stops, UI logic independent of the
// renderer, and structural changes deferred to the flush.

#include "AppModuleTestAccess.h"

#include "Core/Event.h"
#include "Core/System/System.h"
#include "Core/System/VirtualFileSystem.h"
#include "ECS/Systems/Components/LuaScriptComponent.h"
#include "ECS/Systems/LuaScriptingSystem.h"
#include "GUI/Widgets/UIDocument.h"
#include "GUI/Widgets/UIDocumentStore.h"
#include "GUI/Widgets/UITypeIds.h"
#include "GUI/Widgets/UITypeRegistry.h"
#include "GameRuntime/GUI/GameUI/GameUIHost.h"
#include "Scene/Core/Scene.h"

#include <gtest/gtest.h>

#include <format>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ya
{

namespace
{

/// One Lua state over one scene, with scripts served from memory. Every
/// callback appends "<callback>:<name>" to the global TRACE table.
struct FLuaWorld
{
    Scene                              scene{"World"};
    Scene*                             active = &scene;
    LuaScriptingSystem                 lua;
    std::map<std::string, std::string> sources;

    FLuaWorld()
    {
        lua.setRuntimeServices({
            .activeScene = [this]() { return active; },
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
        lua.lua()["TRACE"] = lua.lua().create_table();
        lua.lua().set_function("queueDestroy", [this](const std::string& name) {
            if (Node* node = findNode(name)) {
                scene.queueDestroyNode(node);
            }
        });
        lua.lua().set_function("spawnScripted", [this](const std::string& name) { addScripted(name); });
        lua.lua().set_function("removeScript", [this](const std::string& node, const std::string& script) {
            if (Node* target = findNode(node)) {
                target->getEntity()->getComponent<LuaScriptComponent>()->removeScript(
                    LuaScriptInstance::normalizeScriptPath("Test/Scripts/" + script + ".lua"));
            }
        });
    }

    ~FLuaWorld() { lua.onStop(); }

    /// `order` is the script-declared default; nullopt declares none.
    std::string defineScript(const std::string& name, std::optional<int> order = std::nullopt, std::string onUpdateBody = {})
    {
        const std::string path = LuaScriptInstance::normalizeScriptPath("Test/Scripts/" + name + ".lua");
        std::string source = "local S = {}\n";
        if (order) {
            source += std::format("S.executionOrder = {}\n", *order);
        }
        source += std::format(
            "function S:onInit() table.insert(TRACE, 'init:{0}') end\n"
            "function S:onStart() table.insert(TRACE, 'start:{0}') end\n"
            "function S:onUpdate(dt) table.insert(TRACE, 'update:{0}') {1} end\n"
            "function S:onDestroy() table.insert(TRACE, 'destroy:{0}') end\n"
            "return S\n",
            name,
            onUpdateBody);
        sources[path] = std::move(source);
        return path;
    }

    /// Node named `name` under `parent`, running Test/Scripts/<name>.lua.
    LuaScriptInstance* addScripted(const std::string& name, Node* parent = nullptr)
    {
        const std::string path = LuaScriptInstance::normalizeScriptPath("Test/Scripts/" + name + ".lua");
        if (!sources.contains(path)) {
            defineScript(name);
        }
        Node3D* node = scene.createNode3D(name, parent);
        auto*   comp = node->getEntity()->addComponent<LuaScriptComponent>();
        return comp->addScript(path);
    }

    Node* findNode(const std::string& name)
    {
        for (auto& [handle, node] : scene._nodeMap) {
            (void)handle;
            if (node->getName() == name) {
                return node.get();
            }
        }
        return nullptr;
    }

    /// TRACE since the last call.
    std::vector<std::string> takeTrace()
    {
        std::vector<std::string> out;
        sol::table trace = lua.lua()["TRACE"];
        for (size_t i = 1; i <= trace.size(); ++i) {
            out.push_back(trace.get<std::string>(i));
        }
        lua.lua()["TRACE"] = lua.lua().create_table();
        return out;
    }

    void flush()
    {
        scene.flushQueuedDestroys([this](Entity& entity) { lua.onEntityDestroying(entity); });
    }
};

using Trace = std::vector<std::string>;

struct CountingSystem final : ISystem
{
    int updates = 0;
    void onUpdate(float) override { ++updates; }
};

struct TickCountingBehavior final : UIBehaviorWith<IUITickable>
{
    int ticks = 0;
    [[nodiscard]] bool wantsTick() const override { return true; }
    void tick(UIElement&, float) override { ++ticks; }
};

} // namespace

TEST(TickOrderTest, WorldScriptsRunByExecutionOrderThenTreeOrder)
{
    FLuaWorld world;
    // Created X, Y, Z, W; tree pre-order is X, Z (under X), Y; W declares -10.
    world.addScripted("X");
    world.addScripted("Y");
    world.addScripted("Z", world.findNode("X"));
    world.defineScript("W", -10);
    world.addScripted("W");

    world.lua.onUpdate(0.016f);
    (void)world.takeTrace();
    world.lua.onUpdate(0.016f);
    EXPECT_EQ(world.takeTrace(), (Trace{"update:W", "update:X", "update:Z", "update:Y"}));
}

TEST(TickOrderTest, WorldScriptOrderFollowsTreeChanges)
{
    FLuaWorld world;
    world.addScripted("A");
    world.addScripted("B");
    world.lua.onUpdate(0.016f);
    (void)world.takeTrace();

    world.scene.moveNode(world.findNode("B"), nullptr, 0);
    world.lua.onUpdate(0.016f);
    EXPECT_EQ(world.takeTrace(), (Trace{"update:B", "update:A"})) << "sibling reorder";

    world.scene.moveNode(world.findNode("B"), world.findNode("A"), 0);
    world.addScripted("C", world.findNode("A"));
    world.lua.onUpdate(0.016f);
    EXPECT_EQ(world.takeTrace(), (Trace{"init:C", "start:C", "update:A", "update:B", "update:C"}))
        << "reparent under A, then a new child after it";
}

TEST(TickOrderTest, InstanceOrderOverridesScriptDefault)
{
    FLuaWorld world;
    world.defineScript("P", -5);
    world.addScripted("P");
    auto* q = world.addScripted("Q");
    q->executionOrderOverride = -10;

    world.lua.onUpdate(0.016f);
    (void)world.takeTrace();
    world.lua.onUpdate(0.016f);
    EXPECT_EQ(world.takeTrace(), (Trace{"update:Q", "update:P"}));

    nlohmann::json json;
    world.findNode("Q")->getEntity()->getComponent<LuaScriptComponent>()->serializeCustom(json);
    EXPECT_EQ(json["scripts"][0]["executionOrder"], -10);
    world.findNode("P")->getEntity()->getComponent<LuaScriptComponent>()->serializeCustom(json);
    EXPECT_FALSE(json["scripts"][0].contains("executionOrder"));
}

TEST(TickOrderTest, NewInstancesInitAllThenStartAllBeforeAnyUpdate)
{
    FLuaWorld world;
    world.addScripted("A");
    world.addScripted("B");

    world.lua.onUpdate(0.016f);
    EXPECT_EQ(world.takeTrace(),
              (Trace{"init:A", "init:B", "start:A", "start:B", "update:A", "update:B"}));

    world.lua.onUpdate(0.016f);
    EXPECT_EQ(world.takeTrace(), (Trace{"update:A", "update:B"}));
}

TEST(TickOrderTest, ScriptBaseInstanceResolvesUndefinedCallbacksThroughItsClass)
{
    FLuaWorld world;
    const std::string path = LuaScriptInstance::normalizeScriptPath("Test/Scripts/Derived.lua");
    world.sources[path] =
        "local S = require('ScriptBase'):new()\n"
        "function S:onUpdate(dt) table.insert(TRACE, 'update:Derived') end\n"
        "return S\n";
    auto* script = world.addScripted("Derived");

    world.lua.onUpdate(0.016f);
    ASSERT_TRUE(script->bLoaded);
    EXPECT_EQ(script->executionOrder(), 0);
    EXPECT_EQ(world.takeTrace(), (Trace{"update:Derived"}));
}

TEST(TickOrderTest, ScriptRemovedDuringUpdateDoesNotSkipItsSiblings)
{
    FLuaWorld world;
    auto* comp = world.scene.createNode3D("E")->getEntity()->addComponent<LuaScriptComponent>();
    comp->addScript(world.defineScript("A", std::nullopt, "if self.armed then removeScript('E', 'A') end; self.armed = true"));
    comp->addScript(world.defineScript("B"));

    world.lua.onUpdate(0.016f);
    (void)world.takeTrace();
    // A removes itself mid-frame; B moves from index 1 to 0 and must still run.
    world.lua.onUpdate(0.016f);
    EXPECT_EQ(world.takeTrace(), (Trace{"update:A", "update:B"}));
    world.lua.onUpdate(0.016f);
    EXPECT_EQ(world.takeTrace(), (Trace{"update:B"}));
}

TEST(TickOrderTest, SceneGoneWithoutStopLeavesNoLiveHost)
{
    FLuaWorld world;
    world.addScripted("A");
    world.lua.onUpdate(0.016f);
    ASSERT_EQ(world.lua.liveCount(), 1u);
    (void)world.takeTrace();

    // The scene stops being active without a play stop (unload during play).
    world.active = nullptr;
    EXPECT_EQ(world.lua.liveCount(), 0u);
    world.lua.onStop();
    EXPECT_TRUE(world.takeTrace().empty());

    // Back so teardown's onStop drops the rows' handles before the state dies.
    world.active = &world.scene;
}

TEST(TickOrderTest, InstanceCreatedDuringUpdateStartsNextFrame)
{
    FLuaWorld world;
    world.defineScript("Spawner", std::nullopt, "if not self.done then self.done = true; spawnScripted('N') end");
    world.addScripted("Spawner");

    world.lua.onUpdate(0.016f);
    EXPECT_EQ(world.takeTrace(), (Trace{"init:Spawner", "start:Spawner", "update:Spawner"}));

    world.lua.onUpdate(0.016f);
    EXPECT_EQ(world.takeTrace(), (Trace{"init:N", "start:N", "update:Spawner", "update:N"}));
}

TEST(TickOrderTest, DestroyDuringUpdateIsDeferredToFlush)
{
    FLuaWorld world;
    world.defineScript("Killer", -1, "queueDestroy('Victim')");
    world.addScripted("Killer");
    world.addScripted("Victim");
    world.lua.onUpdate(0.016f);
    (void)world.takeTrace();

    world.lua.onUpdate(0.016f);
    // Queued, not destroyed: the victim still runs this frame and still exists.
    EXPECT_EQ(world.takeTrace(), (Trace{"update:Killer", "update:Victim"}));
    ASSERT_NE(world.findNode("Victim"), nullptr);
    EXPECT_TRUE(world.scene.hasQueuedDestroys());

    world.flush();
    EXPECT_EQ(world.takeTrace(), (Trace{"destroy:Victim"}));
    EXPECT_EQ(world.findNode("Victim"), nullptr);
    EXPECT_FALSE(world.scene.hasQueuedDestroys());

    world.lua.onUpdate(0.016f);
    EXPECT_EQ(world.takeTrace(), (Trace{"update:Killer"}));
}

class TickOrderAppTest : public ::testing::Test
{
  protected:
    App                           app;
    std::unique_ptr<SceneManager> sceneManager;
    std::shared_ptr<Scene>        scene;
    std::shared_ptr<CountingSystem> engineSystem     = std::make_shared<CountingSystem>();
    std::shared_ptr<CountingSystem> simulationSystem = std::make_shared<CountingSystem>();
    std::shared_ptr<TickCountingBehavior> uiBehavior = std::make_shared<TickCountingBehavior>();

    void SetUp() override
    {
        VirtualFileSystem::init();
        sceneManager = std::make_unique<SceneManager>();
        AppModuleTestAccess::setSceneManager(app, sceneManager.get());
        scene = makeShared<Scene>("Play");
        ASSERT_TRUE(sceneManager->activateScene(scene));
        AppModuleTestAccess::setAppState(app, AppState::Runtime);

        AppModuleTestAccess::addSystem(app, engineSystem, ESystemTickGroup::Engine);
        AppModuleTestAccess::addSystem(app, simulationSystem, ESystemTickGroup::Simulation);

        auto host = std::make_unique<GameUIHost>();
        host->onSceneActivated(*scene);
        UIElementRef widget = UITypeRegistry::instance().createInstance(kTypeIdBorder);
        widget->addBehavior(uiBehavior);
        host->getTree().attach(*host->getTree().getLayer(WidgetTree::ELayer::Content), widget);
        AppModuleTestAccess::setGameUIHost(app, std::move(host));
    }

    void TearDown() override
    {
        AppModuleTestAccess::setLuaScriptingSystem(app, nullptr);
        AppModuleTestAccess::setGameUIHost(app, nullptr);
        AppModuleTestAccess::clearSystems(app);
        AppModuleTestAccess::setAppState(app, AppState::Stopped);
        AppModuleTestAccess::setSceneManager(app, nullptr);
        sceneManager.reset();
    }
};

TEST_F(TickOrderAppTest, PauseKeepsEngineMaintenanceSystemsRunning)
{
    AppModuleTestAccess::tickLogic(app, 0.016f);
    EXPECT_EQ(engineSystem->updates, 1);
    EXPECT_EQ(simulationSystem->updates, 1);

    app.pushGamePause();
    AppModuleTestAccess::tickLogic(app, 0.016f);
    EXPECT_EQ(engineSystem->updates, 2);
    EXPECT_EQ(simulationSystem->updates, 1);
}

TEST_F(TickOrderAppTest, PauseStopsGameLogicButNotUILogicOrInputState)
{
    LuaScriptingSystem lua;
    lua.setRuntimeServices({
        .activeScene = [this]() { return scene.get(); },
        .readScript  = [](const std::string&, std::string& out) {
            out = "local S = {}\nfunction S:onUpdate(dt) UPDATES = (UPDATES or 0) + 1 end\nreturn S\n";
            return true;
        },
    });
    lua.init();
    AppModuleTestAccess::setLuaScriptingSystem(app, &lua);
    scene->createNode3D("Scripted")->getEntity()->addComponent<LuaScriptComponent>()->addScript("Test/Scripts/Count.lua");

    AppModuleTestAccess::tickLogic(app, 0.016f);
    EXPECT_EQ(lua.lua().get<int>("UPDATES"), 1);
    EXPECT_EQ(uiBehavior->ticks, 1);

    app.pushGamePause();
    KeyPressedEvent press{};
    press._keyCode = EKey::K_A;
    app.getInputManager().processEvent(press);
    EXPECT_TRUE(app.getInputManager().wasKeyPressed(EKey::K_A));

    AppModuleTestAccess::tickLogic(app, 0.016f);
    EXPECT_EQ(lua.lua().get<int>("UPDATES"), 1);
    EXPECT_EQ(uiBehavior->ticks, 2);
    // The press edge is consumed by this frame even though the game is paused.
    EXPECT_FALSE(app.getInputManager().wasKeyPressed(EKey::K_A));
    EXPECT_TRUE(app.getInputManager().isKeyPressed(EKey::K_A));

    lua.onStop();
    AppModuleTestAccess::setLuaScriptingSystem(app, nullptr);
}

TEST_F(TickOrderAppTest, NestedPauseRequiresMatchingResume)
{
    EXPECT_FALSE(app.isPaused());
    app.pushGamePause();
    app.pushGamePause();
    app.popGamePause();
    EXPECT_TRUE(app.isPaused());
    app.popGamePause();
    EXPECT_FALSE(app.isPaused());
    app.popGamePause();
    EXPECT_FALSE(app.isPaused());
    app.pushGamePause();
    EXPECT_TRUE(app.isPaused());
}

TEST_F(TickOrderAppTest, UILogicRunsWithoutRenderer)
{
    ASSERT_FALSE(app.getRenderServices().hasRenderer());
    AppModuleTestAccess::tickLogic(app, 0.016f);
    EXPECT_EQ(uiBehavior->ticks, 1);

    // Authoring (Stopped) does not run game UI logic.
    AppModuleTestAccess::setAppState(app, AppState::Stopped);
    AppModuleTestAccess::tickLogic(app, 0.016f);
    EXPECT_EQ(uiBehavior->ticks, 1);
}

TEST_F(TickOrderAppTest, StopRemountsSceneUIFromItsDocuments)
{
    GameUIHost*     host = app.getGameUIHost();
    UIDocumentStore documents;
    auto            document = std::make_shared<UIDocument>();
    document->typeId         = kTypeIdBorder;
    documents.put("Test/UI/HUD.yaui", document);
    host->setDocumentStore(&documents);
    SceneWidgetEntry entry;
    entry.entryId      = "HUD";
    entry.documentPath = "Test/UI/HUD.yaui";
    entry.autoMount    = true;
    scene->addWidgetEntry(entry);
    host->reloadMountedSceneUI();

    UIElementRef played = host->findEntryRoot("HUD");
    ASSERT_NE(played, nullptr);
    played->setVisibility(EWidgetVisibility::Hidden);

    app.stopRuntime();
    UIElementRef stopped = host->findEntryRoot("HUD");
    ASSERT_NE(stopped, nullptr);
    EXPECT_NE(stopped, played) << "the played instance is not kept into editing";
    EXPECT_EQ(stopped->getVisibility(), EWidgetVisibility::Visible);
    host->setDocumentStore(nullptr);
}

} // namespace ya
