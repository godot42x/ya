// Phase 3 regression guards for the GameUIHost: scene lifecycle mounts/
// unmounts authoring entries, addToWorld attaches dynamic widgets, input
// routes into the presentation tree, and presentation mapping is exact.
//
// Surface note: `engine.panel` is a canvas LAYOUT host that deliberately does
// not paint (see UICanvasPanel); the painted rect is `engine.border`. Tests
// that assert on produced draw items mount a Border.

#include "GameRuntime/GUI/GameUI/GameUIHost.h"

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

} // namespace ya
