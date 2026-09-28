// Phase 3 regression guards for the GameUIHost: scene lifecycle mounts/
// unmounts authoring entries, addToWorld attaches dynamic widgets, input
// routes into the presentation tree, and presentation mapping is exact.
//
// Surface note: `engine.panel` is a canvas LAYOUT host that deliberately does
// not paint (see UICanvasPanel); the painted rect is `engine.border`. Tests
// that assert on produced draw items mount a Border.

#include "GameRuntime/GUI/GameUI/GameUIHost.h"

#include "Core/Event.h"

#include "GUI/Widgets/Controls/Border.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/UIDocument.h"
#include "GUI/Widgets/UIDocumentStore.h"
#include "GUI/Widgets/UITypeRegistry.h"
#include "Scene/Core/Scene.h"

#include <gtest/gtest.h>

namespace ya
{

namespace
{

/// Counts frames the host tree actually ticked. A behaviour is the framework's
/// own second door into the frame lifecycle, so this asserts the host drives
/// `WidgetTree::tick` rather than only laying the tree out for a snapshot.
struct TickCountingBehavior final : public UIBehavior
{
    int ticks = 0;

    [[nodiscard]] bool wantsTick() const override { return true; }
    void tick(UIElement& owner, float deltaSeconds) override
    {
        (void)owner;
        (void)deltaSeconds;
        ++ticks;
    }
};

/// Accumulates the delta a hosted tree was advanced by. The clock policy decides
/// a VALUE (how much time passed), not whether the tree is visited at all: a
/// paused gameplay frame still walks the tree, it just hands it zero seconds.
/// Counting calls would measure the wrong thing.
struct AdvancingBehavior final : public UIBehavior
{
    float seconds = 0.0f;

    [[nodiscard]] bool wantsTick() const override { return true; }
    void tick(UIElement& owner, float deltaSeconds) override
    {
        (void)owner;
        seconds += deltaSeconds;
    }
};

/// Publish one live document under a test asset path. The mount path resolves
/// through the store, so a test does not need a file on disk.
std::string publishDocument(UIDocumentStore&     store,
                            std::string_view     name,
                            const std::string&   typeId,
                            nlohmann::json       fields = nlohmann::json::object())
{
    auto document    = std::make_shared<UIDocument>();
    document->typeId = typeId;
    document->fields = std::move(fields);
    std::string path = "Test/UI/" + std::string(name) + ".yaui";
    store.put(path, std::move(document));
    return path;
}

SceneWidgetEntry makeEntry(const std::string& entryId, const std::string& documentPath, int32_t zOrder)
{
    SceneWidgetEntry entry;
    entry.entryId      = entryId;
    entry.documentPath = documentPath;
    entry.zOrder       = zOrder;
    entry.autoMount    = true;
    return entry;
}

/// Test controller holding a widget across scene switches (persistent UI:
/// the project keeps references outside the default per-scene tracking).
struct TestPersistentController : public IGameUIController
{
    UIElementRef persistent;
    int          mounts = 0;

    void onSceneActivated(Scene& scene, GameUIHost& host) override
    {
        (void)scene;
        if (!persistent) {
            persistent = UITypeRegistry::instance().createInstance("engine.panel");
            persistent->_name = "Persistent";
            ++mounts;
            host.getTree().attach(*host.getTree().getLayer(WidgetTree::ELayer::Content), persistent);
        }
    }

    void onSceneDeactivated(Scene& scene, GameUIHost& host) override
    {
        (void)scene;
        (void)host;
        // Persistent by contract: never unmount.
    }

    [[nodiscard]] WidgetAttachment addToWorld(Scene& world, const UIElementRef& widget, GameUIHost& host) override
    {
        (void)world;
        return host.getTree().attach(*host.getTree().getLayer(WidgetTree::ELayer::Content), widget);
    }
};

} // namespace

TEST(GameUIHostTest, ActivateMountsAutoMountEntriesByZOrder)
{
    GameUIHost host;
    UIDocumentStore documents;
    host.setDocumentStore(&documents);
    host.setPresentation(Rect2D{.pos = {0.0f, 0.0f}, .extent = {800.0f, 600.0f}}, {1.0f, 1.0f});

    Scene scene("World");
    scene.addWidgetEntry(makeEntry("A", publishDocument(documents, "A", "engine.panel"), 0));
    scene.addWidgetEntry(makeEntry("B", publishDocument(documents, "B", "engine.button"), 10));

    host.onSceneActivated(scene);

    UIElement* content = host.getTree().getLayer(WidgetTree::ELayer::Content);
    ASSERT_EQ(content->getChildren().size(), 2u);
    EXPECT_EQ(content->getChildren()[0]->_typeId, "engine.panel");
    EXPECT_EQ(content->getChildren()[0]->_zOrder, 0);
    EXPECT_EQ(content->getChildren()[1]->_typeId, "engine.button");
    EXPECT_EQ(content->getChildren()[1]->_zOrder, 10);
    EXPECT_EQ(host.getMountedScene(), &scene);
    const auto* rootSlot = dynamic_cast<const UICanvasSlot*>(content->getSlotForChild(*content->getChildren()[0]));
    ASSERT_NE(rootSlot, nullptr);
    EXPECT_EQ(rootSlot->getAnchorMax(), glm::vec2(1.0f, 1.0f));
}

TEST(GameUIHostTest, SceneSwitchUnmountsPreviousAndMountsNext)
{
    GameUIHost host;
    UIDocumentStore documents;
    host.setDocumentStore(&documents);
    host.setPresentation(Rect2D{.pos = {0.0f, 0.0f}, .extent = {800.0f, 600.0f}}, {1.0f, 1.0f});

    Scene sceneA("A");
    sceneA.addWidgetEntry(makeEntry("A1", publishDocument(documents, "A1", "engine.panel"), 0));
    Scene sceneB("B");
    sceneB.addWidgetEntry(makeEntry("B1", publishDocument(documents, "B1", "engine.button"), 5));

    host.onSceneActivated(sceneA);
    host.onSceneActivated(sceneB);

    UIElement* content = host.getTree().getLayer(WidgetTree::ELayer::Content);
    ASSERT_EQ(content->getChildren().size(), 1u);
    EXPECT_EQ(content->getChildren()[0]->_typeId, "engine.button");
    EXPECT_EQ(host.getMountedScene(), &sceneB);

    host.onSceneDeactivated(sceneB);
    EXPECT_EQ(content->getChildren().size(), 0u);
    EXPECT_EQ(host.getMountedScene(), nullptr);
}

TEST(GameUIHostTest, AddToWorldRequiresPresentedWorld)
{
    GameUIHost host;
    host.setPresentation(Rect2D{.pos = {0.0f, 0.0f}, .extent = {800.0f, 600.0f}}, {1.0f, 1.0f});

    Scene sceneA("A");
    Scene sceneB("B");
    host.onSceneActivated(sceneA);

    auto widget = UITypeRegistry::instance().createInstance("engine.panel");
    ASSERT_NE(widget, nullptr);

    // Presented world: attaches to the content layer.
    auto attach = host.addToWorld(sceneA, widget);
    EXPECT_TRUE(attach.valid());
    EXPECT_EQ(host.getTree().getLayer(WidgetTree::ELayer::Content)->getChildren().size(), 1u);

    // Non-presented world: explicit failure, never a silent mount elsewhere.
    auto other = host.addToWorld(sceneB, widget);
    EXPECT_FALSE(other.valid());
}

TEST(GameUIHostTest, InputRoutesThroughPresentationMapping)
{
    GameUIHost host;
    // Viewport offset (100, 50), framebuffer scale 2: logical (0,0) == window (100,50).
    host.setPresentation(Rect2D{.pos = {100.0f, 50.0f}, .extent = {400.0f, 300.0f}}, {2.0f, 2.0f});

    Scene scene("World");
    host.onSceneActivated(scene);

    auto button = std::make_shared<UIButton>("OK");
    FCanvasSlotArgs buttonSlot;
    buttonSlot.offset = {100.0f, 100.0f};
    buttonSlot.fixedSize = {80.0f, 32.0f};
    host.addToWorld(scene, button, buttonSlot);

    int clicks = 0;
    button->_onClick = [&] { ++clicks; };
    // The frame builds the snapshot (layout) before input dispatch, matching
    // the runtime order; without it the hit test would see stale rects.
    host.buildSnapshot();

    // Window point maps to logical (120,110): inside the button.
    EXPECT_EQ(host.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), {100.0f + 240.0f, 50.0f + 220.0f}),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(host.dispatchEvent(MouseButtonReleasedEvent(EMouse::Left), {100.0f + 240.0f, 50.0f + 220.0f}),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(clicks, 1);

    // Outside the viewport: not routed at all.
    EXPECT_EQ(host.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), {10.0f, 10.0f}),
              EWidgetRouteResult::NotHandled);
    EXPECT_EQ(clicks, 1);
}

TEST(GameUIHostTest, BuildSnapshotComposesMountedWidgets)
{
    GameUIHost host;
    UIDocumentStore documents;
    host.setDocumentStore(&documents);
    host.setPresentation(Rect2D{.pos = {0.0f, 0.0f}, .extent = {800.0f, 600.0f}}, {1.0f, 1.0f});

    Scene scene("World");
    scene.addWidgetEntry(makeEntry("P", publishDocument(documents, "P", kTypeIdBorder), 0));
    host.onSceneActivated(scene);

    const UIFrameSnapshot snapshot = host.buildSnapshot();
    ASSERT_EQ(snapshot.items.size(), 1u);
    EXPECT_EQ(snapshot.items[0].kind, UIFrameDrawItem::EKind::Sprite);
    EXPECT_EQ(snapshot.logicalExtent.width, 800u);
}

TEST(GameUIHostTest, DocumentFieldsApplyOnActivation)
{
    GameUIHost host;
    UIDocumentStore documents;
    host.setDocumentStore(&documents);
    host.setPresentation(Rect2D{.pos = {0.0f, 0.0f}, .extent = {800.0f, 600.0f}}, {1.0f, 1.0f});

    Scene scene("World");
    SceneWidgetEntry entry;
    entry.entryId      = "HUD";
    entry.documentPath = publishDocument(documents,
                                         "HUD",
                                         "engine.border",
                                         nlohmann::json{{"_color", {0.1, 0.2, 0.3, 0.9}}});
    entry.autoMount = true;
    scene.addWidgetEntry(std::move(entry));

    host.onSceneActivated(scene);

    UIElement* content = host.getTree().getLayer(WidgetTree::ELayer::Content);
    ASSERT_EQ(content->getChildren().size(), 1u);
    EXPECT_EQ(content->getChildren()[0]->_typeId, "engine.border");
    auto* panel = dynamic_cast<UIBorder*>(content->getChildren()[0].get());
    ASSERT_NE(panel, nullptr);
    EXPECT_EQ(panel->getColor(), glm::vec4(0.1f, 0.2f, 0.3f, 0.9f));
}

TEST(GameUIHostTest, PersistentWidgetSurvivesSceneSwitch)
{
    GameUIHost host;
    host.setPresentation(Rect2D{.pos = {0.0f, 0.0f}, .extent = {800.0f, 600.0f}}, {1.0f, 1.0f});
    host.setController(std::make_unique<TestPersistentController>());

    Scene sceneA("A");
    Scene sceneB("B");
    host.onSceneActivated(sceneA);
    host.onSceneActivated(sceneB);

    // The persistent widget mounted once, kept across the scene switch.
    UIElement* content = host.getTree().getLayer(WidgetTree::ELayer::Content);
    ASSERT_EQ(content->getChildren().size(), 1u);
    EXPECT_EQ(content->getChildren()[0]->_name, "Persistent");
    auto* controller = static_cast<TestPersistentController*>(host.getController());
    EXPECT_EQ(controller->mounts, 1);
}

TEST(GameUIHostTest, ControllerReplacementPerformsHandover)
{
    GameUIHost host;
    UIDocumentStore documents;
    host.setDocumentStore(&documents);
    host.setPresentation(Rect2D{.pos = {0.0f, 0.0f}, .extent = {800.0f, 600.0f}}, {1.0f, 1.0f});

    Scene scene("World");
    scene.addWidgetEntry(makeEntry("A", publishDocument(documents, "A", "engine.panel"), 0));
    scene.addWidgetEntry(makeEntry("B", publishDocument(documents, "B", "engine.button"), 10));
    host.onSceneActivated(scene);
    ASSERT_EQ(host.getTree().getLayer(WidgetTree::ELayer::Content)->getChildren().size(), 2u);

    // Runtime replacement: the old controller's attachments are unmounted,
    // then the new controller mounts the presented scene fresh.
    host.setController(std::make_unique<TestPersistentController>());

    UIElement* content = host.getTree().getLayer(WidgetTree::ELayer::Content);
    ASSERT_EQ(content->getChildren().size(), 1u);
    EXPECT_EQ(content->getChildren()[0]->_name, "Persistent");
}

TEST(GameUIHostTest, PieRestartDoesNotAccumulateWidgets)
{
    GameUIHost host;
    UIDocumentStore documents;
    host.setDocumentStore(&documents);
    host.setPresentation(Rect2D{.pos = {0.0f, 0.0f}, .extent = {800.0f, 600.0f}}, {1.0f, 1.0f});

    Scene scene("World");
    scene.addWidgetEntry(makeEntry("HUD", publishDocument(documents, "HUD", "engine.panel"), 0));
    host.onSceneActivated(scene);

    auto dynamic = UITypeRegistry::instance().createInstance("engine.button");
    host.addToWorld(scene, dynamic);
    ASSERT_EQ(host.getTree().getLayer(WidgetTree::ELayer::Content)->getChildren().size(), 2u);

    // PIE exit: deactivate unmounts entries AND world-scoped dynamic widgets.
    host.onSceneDeactivated(scene);
    EXPECT_EQ(host.getTree().getLayer(WidgetTree::ELayer::Content)->getChildren().size(), 0u);

    // PIE re-enter: fresh mount, no accumulation.
    host.onSceneActivated(scene);
    EXPECT_EQ(host.getTree().getLayer(WidgetTree::ELayer::Content)->getChildren().size(), 1u);
}

TEST(GameUIHostTest, DocumentReferenceSurvivesClone)
{
    GameUIHost host;
    UIDocumentStore documents;
    host.setDocumentStore(&documents);
    host.setPresentation(Rect2D{.pos = {0.0f, 0.0f}, .extent = {800.0f, 600.0f}}, {1.0f, 1.0f});

    Scene scene("Authoring");
    SceneWidgetEntry entry;
    entry.entryId      = "HUD";
    entry.documentPath = publishDocument(documents,
                                         "Cloned",
                                         "engine.text",
                                         nlohmann::json{{"_text", "Cloned UI"}});
    entry.autoMount = true;
    scene.addWidgetEntry(std::move(entry));

    // PIE clones the authoring scene; the clone's entry still points at the
    // same document and must mount through the same runtime controller path.
    stdptr<Scene> play = scene.clone();
    ASSERT_NE(play, nullptr);
    host.onSceneActivated(*play);

    UIElement* content = host.getTree().getLayer(WidgetTree::ELayer::Content);
    ASSERT_EQ(content->getChildren().size(), 1u);
    EXPECT_EQ(content->getChildren()[0]->_typeId, "engine.text");
}

TEST(GameUIHostTest, UpdateAdvancesMountedTreeBehaviors)
{
    GameUIHost host;
    UIDocumentStore documents;
    host.setDocumentStore(&documents);
    host.setPresentation(Rect2D{.pos = {0.0f, 0.0f}, .extent = {800.0f, 600.0f}}, {1.0f, 1.0f});

    Scene scene("World");
    scene.addWidgetEntry(makeEntry("HUD", publishDocument(documents, "HUD", kTypeIdBorder), 0));
    host.onSceneActivated(scene);

    UIElement* content = host.getTree().getLayer(WidgetTree::ELayer::Content);
    ASSERT_EQ(content->getChildren().size(), 1u);
    auto behavior = std::make_shared<TickCountingBehavior>();
    content->getChildren()[0]->addBehavior(behavior);

    // Mounting alone never ticks: the frame driver owns that, and a snapshot is
    // not a frame.
    EXPECT_EQ(behavior->ticks, 0);
    (void)host.buildSnapshot();
    EXPECT_EQ(behavior->ticks, 0);

    host.update(FUIFrameClock{.gameDelta = 1.0f / 60.0f, .realDelta = 1.0f / 60.0f});
    host.update(FUIFrameClock{.gameDelta = 1.0f / 60.0f, .realDelta = 1.0f / 60.0f});
    EXPECT_EQ(behavior->ticks, 2);
}

TEST(GameUIHostTest, ClockPolicyDecidesWhetherPausedFramesAdvanceTheTree)
{
    // A paused frame carries no game time but real wall time. Which one drives
    // the tree is the host's declared policy, so a pause menu can keep animating
    // while a gameplay HUD holds still -- from the same two deltas.
    const FUIFrameClock pausedFrame{.gameDelta = 0.0f, .realDelta = 1.0f / 60.0f};

    GameUIHost host;
    UIDocumentStore documents;
    host.setDocumentStore(&documents);
    host.setPresentation(Rect2D{.pos = {0.0f, 0.0f}, .extent = {800.0f, 600.0f}}, {1.0f, 1.0f});
    Scene scene("World");
    scene.addWidgetEntry(makeEntry("HUD", publishDocument(documents, "HUD", kTypeIdBorder), 0));
    host.onSceneActivated(scene);

    UIElement* content = host.getTree().getLayer(WidgetTree::ELayer::Content);
    ASSERT_EQ(content->getChildren().size(), 1u);
    auto behavior = std::make_shared<AdvancingBehavior>();
    content->getChildren()[0]->addBehavior(behavior);

    constexpr float kFrame = 1.0f / 60.0f;

    // Default is RealTime: paused frames still advance (pause menus animate).
    EXPECT_EQ(host.updateClock(), EUIUpdateClock::RealTime);
    host.update(pausedFrame);
    EXPECT_FLOAT_EQ(behavior->seconds, kFrame);

    // Declared as gameplay UI, the same paused frame advances it by nothing.
    host.setUpdateClock(EUIUpdateClock::GameTime);
    host.update(pausedFrame);
    EXPECT_FLOAT_EQ(behavior->seconds, kFrame);

    // A running frame advances it again. Only pause was holding it back.
    host.update(FUIFrameClock{.gameDelta = kFrame, .realDelta = kFrame});
    EXPECT_FLOAT_EQ(behavior->seconds, 2.0f * kFrame);
}

nlohmann::json testCanvasSlot(float x, float y, float w, float h)
{
    return {
        {"type", "canvas"},
        {"anchorMin", {0.0f, 0.0f}},
        {"anchorMax", {0.0f, 0.0f}},
        {"offset", {x, y}},
        {"minSize", {0.0f, 0.0f}},
        {"maxSize", {1000000.0f, 1000000.0f}},
        {"offsets", {{"left", 0.0f}, {"top", 0.0f}, {"right", 0.0f}, {"bottom", 0.0f}}},
        {"alignmentH", 0},
        {"alignmentV", 0},
        {"widthSizeMode", 0},
        {"heightSizeMode", 0},
        {"pivot", {0.0f, 0.0f}},
        {"preferredSize", {0.0f, 0.0f}},
        {"fixedSize", {w, h}},
    };
}

UIElement* findNamed(UIElement* node, std::string_view name)
{
    if (!node) {
        return nullptr;
    }
    if (node->_name == name) {
        return node;
    }
    for (const UIElementRef& child : node->getChildren()) {
        if (UIElement* found = findNamed(child.get(), name)) {
            return found;
        }
    }
    return nullptr;
}

TEST(GameUIHostTest, MountedTextVisibilityAndButtonAction)
{
    GameUIHost host;
    UIDocumentStore documents;
    host.setDocumentStore(&documents);
    host.setPresentation(Rect2D{.pos = {0.0f, 0.0f}, .extent = {800.0f, 600.0f}}, {1.0f, 1.0f});

    std::string fired;
    host.setWorldActionHandler([&fired](UIElement& source, std::string_view action) {
        fired = std::string(action) + ":" + source._name;
        return true;
    });

    auto score = std::make_shared<UIDocument>();
    score->typeId = "engine.text";
    score->fields = nlohmann::json{
        {"_text", "Score: 0"},
        {"__base__", {{"UIElement", {{"_name", "Score"}}}}},
    };

    auto label = std::make_shared<UIDocument>();
    label->typeId = "engine.text";
    label->fields = nlohmann::json{{"_text", "Restart"}};

    auto button = std::make_shared<UIDocument>();
    button->typeId = "engine.button";
    button->fields = nlohmann::json{
        {"_action", "restart"},
        {"__base__", {{"UIElement", {{"_name", "Restart"}}}}},
    };
    button->children.push_back(label);
    button->childSlots.push_back(nlohmann::json{
        {"type", "content"},
        {"hAlign", 2},
        {"vAlign", 2},
        {"padding", {{"left", 0.0f}, {"top", 0.0f}, {"right", 0.0f}, {"bottom", 0.0f}}},
        {"preferredSize", {80.0f, 24.0f}},
    });

    auto panel = std::make_shared<UIDocument>();
    panel->typeId = "engine.panel";
    panel->fields = nlohmann::json{
        {"__base__", {{"UIElement", {{"_name", "GameOver"}, {"_visibility", "Hidden"}}}}},
    };
    panel->children.push_back(score);
    panel->children.push_back(button);
    panel->childSlots.push_back(testCanvasSlot(16.0f, 16.0f, 200.0f, 32.0f));
    panel->childSlots.push_back(testCanvasSlot(16.0f, 64.0f, 120.0f, 36.0f));

    const std::string path = "Test/UI/Bridge.yaui.json";
    documents.put(path, panel);

    Scene scene("World");
    scene.addWidgetEntry(makeEntry("GameOver", path, 0));
    host.onSceneActivated(scene);

    EXPECT_TRUE(host.setMountedText("GameOver", "Score", "Score: 4"));
    UIElement* content = host.getTree().getLayer(WidgetTree::ELayer::Content);
    auto* text = dynamic_cast<UIText*>(findNamed(content, "Score"));
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->getText(), "Score: 4");

    UIElement* root = findNamed(content, "GameOver");
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->getVisibility(), EWidgetVisibility::Hidden);
    EXPECT_TRUE(host.setMountedVisible("GameOver", "GameOver", true));
    EXPECT_EQ(root->getVisibility(), EWidgetVisibility::Visible);

    auto* restart = dynamic_cast<UIButton*>(findNamed(content, "Restart"));
    ASSERT_NE(restart, nullptr);
    EXPECT_FALSE(static_cast<bool>(restart->_onClick)) << "action buttons are not bound";
    host.getTree().setFocus(restart);
    KeyPressedEvent enter{};
    enter._keyCode = EKey::Enter;
    (void)host.getTree().dispatchEvent(enter, WidgetEventContext{});
    EXPECT_EQ(fired, "restart:Restart");

    EXPECT_FALSE(host.setMountedText("Missing", "Score", "nope"));
    EXPECT_FALSE(host.setMountedVisible("GameOver", "Missing", false));
}

namespace
{

struct RecordingActivator final : public IGameUIBehaviorRuntime
{
    struct FCall
    {
        std::string widget;
        std::string type;
        std::string entryId;
        UIElement*  entryRoot;
    };
    std::vector<FCall>* calls;

    explicit RecordingActivator(std::vector<FCall>& out) : calls(&out) {}

    void activate(UIElement& widget, const FUIBehaviorSpec& spec, const FUIBehaviorActivation& context) override
    {
        calls->push_back({widget._name, spec.type, std::string(context.entryId), &context.entryRoot});
    }

    void update() override {}
};

} // namespace

TEST(GameUIHostTest, MountActivatesBehaviorSpecsInPreorderWithEntryContext)
{
    auto& registry = UITypeRegistry::instance();
    UIElementRef panel = registry.createInstance("engine.panel");
    UIElementRef start = registry.createInstance("engine.button");
    UIElementRef quit  = registry.createInstance("engine.button");
    panel->_name = "Menu";
    start->_name = "Start";
    quit->_name  = "Quit";
    panel->_behaviorSpecs = {{.type = "a"}};
    start->_behaviorSpecs = {{.type = "b"}, {.type = "c"}};
    quit->_behaviorSpecs  = {{.type = "d"}};
    panel->addDetachedChild(start);
    panel->addDetachedChild(quit);

    UIDocumentStore documents;
    documents.put("Test/UI/Menu.yaui", UIDocument::fromWidget(*panel));
    GameUIHost host;
    host.setDocumentStore(&documents);
    Scene scene("World");
    scene.addWidgetEntry(makeEntry("Menu", "Test/UI/Menu.yaui", 0));

    // Without an activator the specs mount inert.
    host.onSceneActivated(scene);
    UIElement* content = host.getTree().getLayer(WidgetTree::ELayer::Content);
    ASSERT_EQ(content->getChildren().size(), 1u);
    EXPECT_EQ(content->getChildren()[0]->_behaviorSpecs.size(), 1u);

    // Installing one re-mounts the presented scene through it.
    std::vector<RecordingActivator::FCall> calls;
    host.setBehaviorRuntime(std::make_unique<RecordingActivator>(calls));
    ASSERT_EQ(content->getChildren().size(), 1u);
    UIElement* root = content->getChildren()[0].get();
    ASSERT_EQ(calls.size(), 4u);
    const std::vector<std::pair<std::string, std::string>> expected = {
        {"Menu", "a"}, {"Start", "b"}, {"Start", "c"}, {"Quit", "d"}};
    for (size_t i = 0; i < calls.size(); ++i) {
        EXPECT_EQ(calls[i].widget, expected[i].first);
        EXPECT_EQ(calls[i].type, expected[i].second);
        EXPECT_EQ(calls[i].entryId, "Menu");
        EXPECT_EQ(calls[i].entryRoot, root);
    }
}

} // namespace ya
