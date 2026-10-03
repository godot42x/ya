// Phase 3 regression guards for the GameUIHost: scene lifecycle mounts/
// unmounts authoring entries, addToWorld attaches dynamic widgets, input
// routes into the presentation tree, and presentation mapping is exact.
//
// Surface note: `engine.panel` is a canvas LAYOUT host that deliberately does
// not paint (see UICanvasPanel); the painted rect is `engine.border`. Tests
// that assert on produced draw items mount a Border.

#include "GameRuntime/GUI/GameUI/GameUIHost.h"
#include "GameRuntime/HostRenderSettings.h"
#include "GUI/Widgets/TextRaster.h"

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

#include <cmath>
#include <fstream>

namespace ya
{

namespace
{

UIElement* findNamed(UIElement& node, std::string_view name)
{
    if (node._name == name) {
        return &node;
    }
    for (const UIElementRef& child : node.getChildren()) {
        if (!child) {
            continue;
        }
        if (UIElement* found = findNamed(*child, name)) {
            return found;
        }
    }
    return nullptr;
}

/// Counts frames the host tree actually ticked. A behaviour is the framework's
/// own second door into the frame lifecycle, so this asserts the host drives
/// `WidgetTree::tick` rather than only laying the tree out for a snapshot.
struct TickCountingBehavior final : public UIBehaviorWith<TickCountingBehavior, IUITickable>
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
struct AdvancingBehavior final : public UIBehaviorWith<AdvancingBehavior, IUITickable>
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
    button->onClicked.addLambda([&] { ++clicks; });
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

// The reference fit rides the tree's DPI axis (fonts re-rasterize at the
// final pixel size). It is not snapped, and it has no floor: integer font
// raster sizes are the atlas quantum. The logical canvas is viewport / fit.
TEST(GameUIHostTest, ReferenceScaleFollowsTheViewportContinuously)
{
    GameUIHost host;
    host.setReferenceResolution({1000, 1000});

    // 540 / 1000 = 0.54, not a 1/16 step. It stays 0.54.
    host.setPresentation(Rect2D{.pos = {0.0f, 0.0f}, .extent = {540.0f, 540.0f}}, {1.0f, 1.0f});
    const float unsnapped = 540.0f / 1000.0f;
    EXPECT_FLOAT_EQ(host.referenceScale(), unsnapped);
    EXPECT_FLOAT_EQ(host.getTree().getDpiScale(), unsnapped);
    EXPECT_NE(unsnapped, std::floor(unsnapped * 16.0f + 0.5f) / 16.0f);
    // Extent2D stores whole pixels, so 540/0.54 (999.99994) lands on 999.
    EXPECT_EQ(host.getTree().getLogicalExtent().width, static_cast<uint32_t>(540.0f / unsnapped));
    EXPECT_EQ(host.getTree().getLogicalExtent().height, static_cast<uint32_t>(540.0f / unsnapped));

    // Raw fit 0.27 stays 0.27. The canvas is the reference, viewport / scale.
    host.setPresentation(Rect2D{.pos = {0.0f, 0.0f}, .extent = {270.0f, 270.0f}}, {1.0f, 1.0f});
    EXPECT_FLOAT_EQ(host.referenceScale(), 0.27f);
    EXPECT_FLOAT_EQ(host.getTree().getDpiScale(), 0.27f);
    EXPECT_EQ(host.getTree().getLogicalExtent().width, static_cast<uint32_t>(270.0f / 0.27f));
    EXPECT_EQ(host.getTree().getLogicalExtent().height, static_cast<uint32_t>(270.0f / 0.27f));
}

TEST(GameUIHostTest, DensityDoublesRasterAndKeepsTheLogicalCanvas)
{
    GameUIHost host;
    host.setReferenceResolution({1280, 720});

    host.setPresentation(Rect2D{.pos = {0.0f, 0.0f}, .extent = {349.0f, 197.0f}}, {1.0f, 1.0f});
    const float expected = std::min(349.0f / 1280.0f, 197.0f / 720.0f);
    EXPECT_FLOAT_EQ(host.referenceScale(), expected);
    EXPECT_LT(expected, 0.5f);
    const auto  logicalCanvas = host.getTree().getLogicalExtent();
    const float logicalDpi    = host.getTree().getDpiScale();
    EXPECT_FLOAT_EQ(logicalDpi, expected);

    host.setPresentation(Rect2D{.pos = {0.0f, 0.0f}, .extent = {698.0f, 394.0f}}, {2.0f, 2.0f});
    EXPECT_FLOAT_EQ(host.referenceScale(), expected);
    EXPECT_EQ(host.getTree().getLogicalExtent().width, logicalCanvas.width);
    EXPECT_EQ(host.getTree().getLogicalExtent().height, logicalCanvas.height);
    EXPECT_FLOAT_EQ(host.getTree().getDpiScale(), logicalDpi * 2.0f);

    // 13px hint at this fit is under the 9px raster floor on both densities.
    // The floor is a render-layer safety value; the window minimum is what
    // keeps a real presentation from living here.
    const FTextRasterPlan atDensity1 = planTextRaster(13.0f, glm::vec2(1.0f), logicalDpi, glm::vec2(1.0f));
    const FTextRasterPlan atDensity2 = planTextRaster(13.0f, glm::vec2(1.0f), logicalDpi * 2.0f, glm::vec2(1.0f));
    EXPECT_EQ(atDensity1.rasterPx, 9);
    EXPECT_EQ(atDensity2.rasterPx, 9);
}

TEST(GameUIHostTest, WindowPointMapsIntoTheLogicalViewport)
{
    const Rect2D panel{.pos = {40.0f, 20.0f}, .extent = {349.0f, 197.0f}};
    const auto inside = mapWindowPointToViewPixels({50.0f, 30.0f}, panel, 2.0f);
    ASSERT_TRUE(inside.has_value());
    EXPECT_FLOAT_EQ(inside->x, 20.0f);
    EXPECT_FLOAT_EQ(inside->y, 20.0f);

    EXPECT_FALSE(mapWindowPointToViewPixels({10.0f, 30.0f}, panel, 2.0f).has_value());

    const auto wholeWindow = mapWindowPointToViewPixels({12.0f, 8.0f}, {}, 2.0f);
    ASSERT_TRUE(wholeWindow.has_value());
    EXPECT_FLOAT_EQ(wholeWindow->x, 24.0f);
    EXPECT_FLOAT_EQ(wholeWindow->y, 16.0f);

    const auto unit = mapWindowPointToViewPixels({3.0f, 4.0f}, {}, 0.0f);
    ASSERT_TRUE(unit.has_value());
    EXPECT_FLOAT_EQ(unit->x, 3.0f);
    EXPECT_FLOAT_EQ(unit->y, 4.0f);
}

TEST(GameUIHostTest, InputMappingFollowsTheReferenceScale)
{
    GameUIHost host;
    host.setReferenceResolution({1000, 1000});
    host.setPresentation(Rect2D{.pos = {100.0f, 50.0f}, .extent = {400.0f, 300.0f}}, {2.0f, 2.0f});

    Scene scene("World");
    host.onSceneActivated(scene);

    auto button = std::make_shared<UIButton>("OK");
    FCanvasSlotArgs buttonSlot;
    buttonSlot.offset    = {100.0f, 100.0f};
    buttonSlot.fixedSize = {80.0f, 32.0f};
    host.addToWorld(scene, button, buttonSlot);

    int clicks = 0;
    button->onClicked.addLambda([&] { ++clicks; });
    host.buildSnapshot();

    // Logical viewport is 200x150 against a 1000 reference, so the fit is
    // 0.15. The button centre, logical (140,116), lands at
    // window pos + logical * (framebufferScale * referenceScale).
    EXPECT_FLOAT_EQ(host.referenceScale(), 0.15f);
    const float     devicePerLogical = 2.0f * 0.15f;
    const glm::vec2 centre{100.0f + 140.0f * devicePerLogical, 50.0f + 116.0f * devicePerLogical};
    EXPECT_EQ(host.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), centre),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(host.dispatchEvent(MouseButtonReleasedEvent(EMouse::Left), centre),
              EWidgetRouteResult::HandledExclusive);
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

TEST(GameUIHostTest, DueTimersFireEarliestFirstOncePerUpdate)
{
    GameUIHost       host;
    std::vector<int> fired;
    auto             record = [&fired](int tag) {
        return [&fired, tag]() {
            fired.push_back(tag);
            return true;
        };
    };
    (void)host.addTimer(nullptr, 0.3f, 0.0f, record(3));
    const uint64_t cancelled = host.addTimer(nullptr, 0.1f, 0.0f, record(0));
    (void)host.addTimer(nullptr, 0.2f, 0.0f, record(2));
    (void)host.addTimer(nullptr, 0.1f, 0.0f, record(1));
    (void)host.addTimer(nullptr, 0.05f, 0.1f, record(9));
    (void)host.addTimer(nullptr, 0.0f, 0.0f, [&]() {
        fired.push_back(-1);
        (void)host.addTimer(nullptr, 0.0f, 0.0f, record(5));
        return false;
    });
    host.cancelTimer(cancelled);

    host.update(FUIFrameClock{.gameDelta = 0.5f, .realDelta = 0.5f});
    EXPECT_EQ(fired, (std::vector<int>{-1, 9, 1, 2, 3}))
        << "by due time, ties in creation order; a repeat that fell behind fires once";

    fired.clear();
    host.update(FUIFrameClock{.gameDelta = 0.0f, .realDelta = 0.0f});
    EXPECT_EQ(fired, (std::vector<int>{5})) << "a timer added by a callback waits for the next update";
}

TEST(GameUIHostTest, MountedTextVisibilityAndButtonClick)
{
    GameUIHost host;
    UIDocumentStore documents;
    host.setDocumentStore(&documents);
    host.setPresentation(Rect2D{.pos = {0.0f, 0.0f}, .extent = {800.0f, 600.0f}}, {1.0f, 1.0f});

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
    EXPECT_EQ(restart->onClicked.size(), 0u) << "a mounted button carries no handler of its own";
    int clicks = 0;
    restart->onClicked.addLambda([&clicks] { ++clicks; });
    host.getTree().setFocus(restart);
    KeyPressedEvent enter{};
    enter._keyCode = EKey::Enter;
    (void)host.getTree().dispatchEvent(enter, WidgetEventContext{});
    EXPECT_EQ(clicks, 1);

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

TEST(GameUIHostTest, ReferenceResolutionScalesLayoutAndPointer)
{
    GameUIHost host;
    host.setPresentation(Rect2D{.pos = {0.0f, 0.0f}, .extent = {800.0f, 600.0f}}, {1.0f, 1.0f});
    EXPECT_EQ(host.referenceScale(), 1.0f);
    EXPECT_EQ(host.getTree().getLogicalExtent(), (Extent2D{.width = 800, .height = 600}));

    host.setReferenceResolution({1280, 720});

    // Viewport matches half the reference on both axes: scale 0.5, layout is the reference.
    host.setPresentation(Rect2D{.pos = {0.0f, 0.0f}, .extent = {640.0f, 360.0f}}, {1.0f, 1.0f});
    EXPECT_FLOAT_EQ(host.referenceScale(), 0.5f);
    EXPECT_EQ(host.getTree().getLogicalExtent(), (Extent2D{.width = 1280, .height = 720}));

    // Height matches the reference, width is half: width limits the fit.
    host.setPresentation(Rect2D{.pos = {0.0f, 0.0f}, .extent = {640.0f, 720.0f}}, {1.0f, 1.0f});
    EXPECT_FLOAT_EQ(host.referenceScale(), 0.5f);
    EXPECT_EQ(host.getTree().getLogicalExtent(), (Extent2D{.width = 1280, .height = 1440}));

    // Framebuffer scale 2 and a reference fold into one uiScale.
    host.setPresentation(Rect2D{.pos = {10.0f, 20.0f}, .extent = {1280.0f, 720.0f}}, {2.0f, 2.0f});
    EXPECT_FLOAT_EQ(host.referenceScale(), 0.5f);
    EXPECT_EQ(host.getTree().getLogicalExtent(), (Extent2D{.width = 1280, .height = 720}));
    const UIFrameSnapshot snapshot = host.buildSnapshot();
    EXPECT_FLOAT_EQ(snapshot.buildContext.uiScale.x, 1.0f);
    EXPECT_FLOAT_EQ(snapshot.buildContext.uiScale.y, 1.0f);

    Scene scene("World");
    host.onSceneActivated(scene);
    auto button = std::make_shared<UIButton>("OK");
    FCanvasSlotArgs buttonSlot;
    buttonSlot.offset    = {100.0f, 100.0f};
    buttonSlot.fixedSize = {80.0f, 32.0f};
    host.addToWorld(scene, button, buttonSlot);
    int clicks = 0;
    button->onClicked.addLambda([&] { ++clicks; });
    host.buildSnapshot();

    // Layout (120, 110) * framebufferScale * referenceScale + viewport origin.
    const glm::vec2 hit = glm::vec2{10.0f, 20.0f} + glm::vec2{120.0f, 110.0f} * (2.0f * 0.5f);
    EXPECT_EQ(host.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), hit), EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(host.dispatchEvent(MouseButtonReleasedEvent(EMouse::Left), hit), EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(clicks, 1);
}

TEST(GameUIHostTest, DialogueDocumentStaysInsideNarrowAndWideCanvases)
{
    std::ifstream stream("Example/2DRpgPrototype/Content/UI/Dialogue.yaui.json");
    ASSERT_TRUE(stream.is_open());
    const auto document = UIDocument::fromJson(nlohmann::json::parse(stream));
    ASSERT_NE(document, nullptr);

    const auto roundTrip = UIDocument::fromJson(document->toJson());
    ASSERT_NE(roundTrip, nullptr);

    const auto expectInside = [](const std::shared_ptr<UIDocument>& source, Extent2D canvas, float expectedBoxWidth,
                                 float expectedBoxX) {
        const UIElementRef root = source->instantiate();
        ASSERT_NE(root, nullptr);
        WidgetTree tree(canvas);
        FCanvasSlotArgs fill;
        fill.anchorMin = {0.0f, 0.0f};
        fill.anchorMax = {1.0f, 1.0f};
        ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), root, fill).valid());
        tree.layout();

        UIElement* box  = findNamed(*root, "Box");
        UIElement* body = findNamed(*root, "Body");
        UIElement* hint = findNamed(*root, "Hint");
        ASSERT_NE(box, nullptr);
        ASSERT_NE(body, nullptr);
        ASSERT_NE(hint, nullptr);

        const auto* bodyText = dynamic_cast<UIText*>(body);
        ASSERT_NE(bodyText, nullptr);
        EXPECT_TRUE(bodyText->_bWrap);

        EXPECT_FLOAT_EQ(box->_layoutRect.extent.x, expectedBoxWidth);
        EXPECT_FLOAT_EQ(box->_layoutRect.extent.y, 160.0f);
        EXPECT_FLOAT_EQ(box->_layoutRect.pos.x, expectedBoxX);
        EXPECT_GE(box->_layoutRect.pos.x, 0.0f);
        EXPECT_LE(box->_layoutRect.pos.x + box->_layoutRect.extent.x, static_cast<float>(canvas.width) + 0.5f);
        EXPECT_GE(box->_layoutRect.pos.y, 0.0f);
        EXPECT_LE(box->_layoutRect.pos.y + box->_layoutRect.extent.y, static_cast<float>(canvas.height) + 0.5f);

        UIElement* frame = body->getParent();
        ASSERT_NE(frame, nullptr);
        EXPECT_EQ(frame->getParent(), box);
        // Child rects are in the same space as the frame rect (it carries the
        // box's position), so compare against that rect, not its extent alone.
        const Rect2D frameRect = frame->_layoutRect;
        const auto inside = [](const Rect2D& child, const Rect2D& parent) {
            EXPECT_GE(child.pos.x, parent.pos.x - 0.5f);
            EXPECT_GE(child.pos.y, parent.pos.y - 0.5f);
            EXPECT_LE(child.pos.x + child.extent.x, parent.pos.x + parent.extent.x + 0.5f);
            EXPECT_LE(child.pos.y + child.extent.y, parent.pos.y + parent.extent.y + 0.5f);
        };
        inside(body->_layoutRect, frameRect);
        inside(hint->_layoutRect, frameRect);
    };

    expectInside(document, Extent2D{.width = 1280, .height = 720}, 880.0f, 200.0f);
    expectInside(roundTrip, Extent2D{.width = 700, .height = 500}, 636.0f, 32.0f);
}

} // namespace ya
