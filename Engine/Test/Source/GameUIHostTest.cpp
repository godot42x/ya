// Phase 3 regression guards for the GameUIHost: scene lifecycle mounts/
// unmounts authoring entries, addToWorld attaches dynamic widgets, input
// routes into the presentation tree, and presentation mapping is exact.

#include "Host/GUI/GameUI/GameUIHost.h"

#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/UIDocument.h"
#include "GUI/Widgets/UITypeRegistry.h"
#include "Scene/Core/Scene.h"

#include <gtest/gtest.h>

namespace ya
{

namespace
{

SceneWidgetEntry makeEntry(const std::string& entryId, const std::string& typeId, int32_t zOrder)
{
    SceneWidgetEntry entry;
    entry.entryId        = entryId;
    entry.inlineDocument = std::make_shared<UIDocument>();
    entry.inlineDocument->typeId = typeId;
    entry.zOrder    = zOrder;
    entry.autoMount = true;
    return entry;
}

} // namespace

TEST(GameUIHostTest, ActivateMountsAutoMountEntriesByZOrder)
{
    GameUIHost host;
    host.setPresentation(Rect2D{.pos = {0.0f, 0.0f}, .extent = {800.0f, 600.0f}}, {1.0f, 1.0f});

    Scene scene("World");
    scene.addWidgetEntry(makeEntry("A", "engine.panel", 0));
    scene.addWidgetEntry(makeEntry("B", "engine.button", 10));

    host.onSceneActivated(scene);

    UIElement* content = host.getTree().getLayer(WidgetTree::ELayer::Content);
    ASSERT_EQ(content->getChildren().size(), 2u);
    EXPECT_EQ(content->getChildren()[0]->_typeId, "engine.panel");
    EXPECT_EQ(content->getChildren()[0]->_zOrder, 0);
    EXPECT_EQ(content->getChildren()[1]->_typeId, "engine.button");
    EXPECT_EQ(content->getChildren()[1]->_zOrder, 10);
    EXPECT_EQ(host.getMountedScene(), &scene);
}

TEST(GameUIHostTest, SceneSwitchUnmountsPreviousAndMountsNext)
{
    GameUIHost host;
    host.setPresentation(Rect2D{.pos = {0.0f, 0.0f}, .extent = {800.0f, 600.0f}}, {1.0f, 1.0f});

    Scene sceneA("A");
    sceneA.addWidgetEntry(makeEntry("A1", "engine.panel", 0));
    Scene sceneB("B");
    sceneB.addWidgetEntry(makeEntry("B1", "engine.button", 5));

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
    button->_position = {100.0f, 100.0f}; // logical
    button->_size     = {80.0f, 32.0f};
    host.addToWorld(scene, button);

    int clicks = 0;
    button->_onClick = [&] { ++clicks; };
    // The frame builds the snapshot (layout) before input dispatch, matching
    // the runtime order; without it the hit test would see stale rects.
    host.buildSnapshot();

    // Window point maps to logical (120,110): inside the button.
    EXPECT_EQ(host.dispatchEvent(MouseButtonPressedEvent(0), {100.0f + 240.0f, 50.0f + 220.0f}),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(host.dispatchEvent(MouseButtonReleasedEvent(0), {100.0f + 240.0f, 50.0f + 220.0f}),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(clicks, 1);

    // Outside the viewport: not routed at all.
    EXPECT_EQ(host.dispatchEvent(MouseButtonPressedEvent(0), {10.0f, 10.0f}),
              EWidgetRouteResult::NotHandled);
    EXPECT_EQ(clicks, 1);
}

TEST(GameUIHostTest, BuildSnapshotComposesMountedWidgets)
{
    GameUIHost host;
    host.setPresentation(Rect2D{.pos = {0.0f, 0.0f}, .extent = {800.0f, 600.0f}}, {1.0f, 1.0f});

    Scene scene("World");
    scene.addWidgetEntry(makeEntry("P", "engine.panel", 0));
    host.onSceneActivated(scene);

    const UIFrameSnapshot snapshot = host.buildSnapshot();
    ASSERT_EQ(snapshot.items.size(), 1u);
    EXPECT_EQ(snapshot.items[0].kind, UIFrameDrawItem::EKind::Sprite);
    EXPECT_EQ(snapshot.logicalExtent.width, 800u);
}

} // namespace ya
