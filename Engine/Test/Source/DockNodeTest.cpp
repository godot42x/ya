#include "GUI/Widgets/Controls/DockSpace/DockNode.h"
#include "GUI/Widgets/Controls/DockSpace/DockContext.h"
#include "GUI/Widgets/Controls/DockSpace/DockSpace.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/WidgetTree.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <unordered_set>

namespace ya
{

namespace
{
void registerPanel(FDockTreeModel& model, DockPanelId id, const char* key)
{
    ASSERT_TRUE(model.registerPanel({.id = id, .stableKey = key, .title = key}));
}
}

TEST(DockNodeTest, RegistersPanelsAndRejectsDuplicateIdentity)
{
    FDockTreeModel model;
    registerPanel(model, 1, "scene");
    EXPECT_FALSE(model.registerPanel({.id = 1, .stableKey = "other", .title = "Other"}));
    EXPECT_FALSE(model.registerPanel({.id = 2, .stableKey = "scene", .title = "Duplicate key"}));
    EXPECT_TRUE(model.validateInvariants());
}

TEST(DockNodeTest, CardinalSplitCreatesStableBinaryTree)
{
    FDockTreeModel model;
    registerPanel(model, 1, "scene");
    registerPanel(model, 2, "inspector");
    ASSERT_TRUE(model.addPanel(1));
    const DockNodeId rootId = model.getRootNode()->id;

    ASSERT_TRUE(model.splitLeaf(rootId, EDockCardinalSide::East, 2));
    ASSERT_EQ(model.getRootNode()->kind, EDockNodeKind::Split);
    ASSERT_EQ(model.getRootNode()->child[0]->parent, model.getRootNode());
    ASSERT_EQ(model.getRootNode()->child[1]->parent, model.getRootNode());
    EXPECT_EQ(model.getRootNode()->orientation, EDockSplitOrientation::Vertical);
    EXPECT_EQ(model.findLeafForPanel(2)->panelIds, std::vector<DockPanelId>({2}));
    EXPECT_EQ(model.findLeafForPanel(1)->panelIds, std::vector<DockPanelId>({1}));
    EXPECT_TRUE(model.validateInvariants());
}

TEST(DockNodeTest, MoveCollapsesEmptySourceAndPreservesTargetOrder)
{
    FDockTreeModel model;
    registerPanel(model, 1, "scene");
    registerPanel(model, 2, "inspector");
    registerPanel(model, 3, "console");
    ASSERT_TRUE(model.addPanel(1));
    ASSERT_TRUE(model.splitLeaf(model.getRootNode()->id, EDockCardinalSide::West, 2));
    auto* target = model.findLeafForPanel(1);
    auto* source = model.findLeafForPanel(2);
    ASSERT_NE(target, nullptr);
    ASSERT_NE(source, nullptr);
    ASSERT_NE(target, source);
    ASSERT_TRUE(model.addPanel(3, target->id));

    ASSERT_TRUE(model.movePanel(2, target->id, 1));
    EXPECT_EQ(model.getRootNode()->kind, EDockNodeKind::Leaf);
    EXPECT_EQ(model.getRootNode()->panelIds, (std::vector<DockPanelId>{1, 2, 3}));
    EXPECT_EQ(model.getRootNode()->selectedPanel, 2);
    EXPECT_TRUE(model.validateInvariants());
}

TEST(DockNodeTest, InvalidMutationDoesNotChangeModel)
{
    FDockTreeModel model;
    registerPanel(model, 1, "scene");
    registerPanel(model, 2, "inspector");
    ASSERT_TRUE(model.addPanel(1));
    const DockNodeId rootId = model.getRootNode()->id;

    EXPECT_FALSE(model.movePanel(2, rootId));
    EXPECT_FALSE(model.splitLeaf(rootId, EDockCardinalSide::North, 99));
    EXPECT_EQ(model.getRootNode()->kind, EDockNodeKind::Leaf);
    EXPECT_EQ(model.getRootNode()->panelIds, std::vector<DockPanelId>({1}));
    EXPECT_TRUE(model.validateInvariants());
}

TEST(DockNodeTest, MoveToSameLeafNoOpKeepsOrder)
{
    FDockTreeModel model;
    registerPanel(model, 1, "scene");
    ASSERT_TRUE(model.addPanel(1));
    const auto* before = model.findLeafForPanel(1);
    ASSERT_NE(before, nullptr);
    const auto beforePanels = before->panelIds;
    EXPECT_TRUE(model.movePanel(1, before->id));
    ASSERT_NE(model.findLeafForPanel(1), nullptr);
    EXPECT_EQ(model.findLeafForPanel(1)->panelIds, beforePanels);
    EXPECT_TRUE(model.validateInvariants());
}

TEST(DockNodeTest, SplitClampsRatioAndValidatesGeometry)
{
    FDockTreeModel model;
    registerPanel(model, 1, "scene");
    registerPanel(model, 2, "inspector");
    ASSERT_TRUE(model.addPanel(1));
    ASSERT_TRUE(model.splitLeaf(model.getRootNode()->id, EDockCardinalSide::South, 2, 4.0f));
    EXPECT_FLOAT_EQ(model.getRootNode()->ratio, 1.0f);
    model.getRootNode()->minExtent[0] = -1.0f;
    std::string error;
    EXPECT_FALSE(model.validateInvariants(&error));
    EXPECT_FALSE(error.empty());
}

TEST(DockNodeTest, SplitEmptyLeafKeepsPersistentPlaceholder)
{
    FDockTreeModel model;
    registerPanel(model, 1, "scene");
    ASSERT_TRUE(model.addPanel(1));
    const DockNodeId rootId = model.getRootNode()->id;
    ASSERT_TRUE(model.splitEmptyLeaf(rootId, EDockCardinalSide::East));
    const auto leaves = model.leafIds();
    ASSERT_EQ(leaves.size(), 2u);
    const auto* empty = model.findNode(leaves.back());
    ASSERT_NE(empty, nullptr);
    EXPECT_TRUE(empty->panelIds.empty());
    EXPECT_TRUE(empty->persistentEmptyLeaf);
    EXPECT_TRUE(model.validateInvariants());
}

TEST(DockNodeTest, SameLeafMoveReordersTabs)
{
    FDockTreeModel model;
    registerPanel(model, 1, "hierarchy");
    registerPanel(model, 2, "assets");
    ASSERT_TRUE(model.addPanel(1));
    ASSERT_TRUE(model.addPanel(2));
    const DockNodeId leafId = model.getRootNode()->id;
    EXPECT_EQ(model.getRootNode()->panelIds, (std::vector<DockPanelId>{1, 2}));

    ASSERT_TRUE(model.movePanel(2, leafId, 0));
    EXPECT_EQ(model.getRootNode()->panelIds, (std::vector<DockPanelId>{2, 1}));
    EXPECT_EQ(model.getRootNode()->selectedPanel, 2);

    ASSERT_TRUE(model.movePanel(2, leafId, 1));
    EXPECT_EQ(model.getRootNode()->panelIds, (std::vector<DockPanelId>{2, 1}));
    EXPECT_TRUE(model.validateInvariants());
}

TEST(DockNodeTest, RemovePanelDeletesRegistryRecord)
{
    FDockTreeModel model;
    registerPanel(model, 1, "scene");
    ASSERT_TRUE(model.addPanel(1));
    ASSERT_TRUE(model.removePanel(1));
    EXPECT_EQ(model.panelCount(), 0u);
    EXPECT_EQ(model.findPanel(1), nullptr);
    EXPECT_TRUE(model.getRootNode()->panelIds.empty());
    EXPECT_TRUE(model.validateInvariants());
}

TEST(DockNodeTest, DetachFromTreeKeepsRegistryForLaterRedock)
{
    FDockTreeModel model;
    registerPanel(model, 1, "scene");
    registerPanel(model, 2, "inspector");
    ASSERT_TRUE(model.addPanel(1));
    ASSERT_TRUE(model.addPanel(2));

    ASSERT_TRUE(model.detachFromTree(2));
    // Registry record survives and the panel is no longer in any leaf.
    EXPECT_EQ(model.panelCount(), 2u);
    EXPECT_NE(model.findPanel(2), nullptr);
    EXPECT_EQ(model.findLeafForPanel(2), nullptr);
    EXPECT_TRUE(model.validateInvariants());

    // It can be re-docked back into a leaf.
    ASSERT_TRUE(model.addPanel(2, model.getRootNode()->id));
    EXPECT_NE(model.findLeafForPanel(2), nullptr);
    EXPECT_TRUE(model.validateInvariants());

    EXPECT_FALSE(model.detachFromTree(99));
}

TEST(DockNodeTest, SelectionIsOwnedByLeafModel)
{
    FDockTreeModel model;
    registerPanel(model, 1, "scene");
    registerPanel(model, 2, "inspector");
    ASSERT_TRUE(model.addPanel(1));
    ASSERT_TRUE(model.addPanel(2));
    ASSERT_TRUE(model.selectPanel(1));
    EXPECT_EQ(model.findLeafForPanel(1)->selectedPanel, 1);
    EXPECT_FALSE(model.selectPanel(99));
    EXPECT_TRUE(model.validateInvariants());
}

TEST(DockNodeTest, SplitRatioMutationIsClampedAndAtomic)
{
    FDockTreeModel model;
    registerPanel(model, 1, "scene");
    ASSERT_TRUE(model.addPanel(1));
    ASSERT_TRUE(model.splitEmptyLeaf(model.getRootNode()->id, EDockCardinalSide::East));
    const DockNodeId splitId = model.getRootNode()->id;
    ASSERT_TRUE(model.setSplitRatio(splitId, 1.5f));
    EXPECT_FLOAT_EQ(model.getRootNode()->ratio, 1.0f);
    EXPECT_TRUE(model.setSplitRatio(splitId, 0.25f));
    EXPECT_FLOAT_EQ(model.getRootNode()->ratio, 0.25f);
    EXPECT_FALSE(model.setSplitRatio(9999, 0.5f));
    EXPECT_TRUE(model.validateInvariants());
}

TEST(DockNodeTest, SameLeafSplitKeepsOtherPanelsInPlace)
{
    FDockTreeModel model;
    registerPanel(model, 1, "scene");
    registerPanel(model, 2, "inspector");
    ASSERT_TRUE(model.addPanel(1));
    ASSERT_TRUE(model.addPanel(2));
    const DockNodeId rootId = model.getRootNode()->id;

    ASSERT_TRUE(model.splitLeaf(rootId, EDockCardinalSide::West, 2));
    ASSERT_EQ(model.getRootNode()->kind, EDockNodeKind::Split);
    EXPECT_EQ(model.getRootNode()->child[0]->panelIds, (std::vector<DockPanelId>{2}));
    EXPECT_EQ(model.getRootNode()->child[1]->panelIds, (std::vector<DockPanelId>{1}));
    EXPECT_TRUE(model.validateInvariants());
}

TEST(DockNodeTest, SinglePanelSameLeafSplitDoesNotCreateEmptyLeaf)
{
    FDockTreeModel model;
    registerPanel(model, 1, "scene");
    ASSERT_TRUE(model.addPanel(1));
    const DockNodeId rootId = model.getRootNode()->id;

    // A one-panel leaf cannot split its only panel out onto its own edge:
    // that would leave an empty (non-persistent) half.
    EXPECT_FALSE(model.splitLeaf(rootId, EDockCardinalSide::East, 1));
    ASSERT_EQ(model.getRootNode()->kind, EDockNodeKind::Leaf);
    EXPECT_EQ(model.getRootNode()->panelIds, std::vector<DockPanelId>({1}));
    EXPECT_EQ(model.leafIds().size(), 1u);
    EXPECT_TRUE(model.validateInvariants());
}

TEST(DockNodeTest, ExportImportRoundTripPreservesLayout)
{
    FDockTreeModel model;
    registerPanel(model, 1, "viewport");
    registerPanel(model, 2, "hierarchy");
    registerPanel(model, 3, "inspector");
    registerPanel(model, 4, "content-browser");
    ASSERT_TRUE(model.addPanel(1));
    const DockNodeId rootId = model.getRootNode()->id;
    ASSERT_TRUE(model.splitLeaf(rootId, EDockCardinalSide::East, 3, 0.74f));
    if (FDockNode* viewportLeaf = model.findLeafForPanel(1)) {
        ASSERT_TRUE(model.splitLeaf(viewportLeaf->id, EDockCardinalSide::West, 2, 0.26f));
    }
    if (FDockNode* viewportLeaf = model.findLeafForPanel(1)) {
        ASSERT_TRUE(model.splitLeaf(viewportLeaf->id, EDockCardinalSide::South, 4, 0.72f));
    }
    model.selectPanel(4);

    const nlohmann::json layout = model.exportLayoutJson();
    FDockTreeModel restored;
    registerPanel(restored, 1, "viewport");
    registerPanel(restored, 2, "hierarchy");
    registerPanel(restored, 3, "inspector");
    registerPanel(restored, 4, "content-browser");
    ASSERT_TRUE(restored.addPanel(1));
    ASSERT_TRUE(restored.addPanel(2));
    ASSERT_TRUE(restored.addPanel(3));
    ASSERT_TRUE(restored.addPanel(4));
    ASSERT_TRUE(restored.importLayoutJson(layout));

    EXPECT_EQ(restored.getRootNode()->kind, EDockNodeKind::Split);
    EXPECT_FLOAT_EQ(restored.getRootNode()->ratio, model.getRootNode()->ratio);
    ASSERT_NE(restored.findLeafForPanel(1), nullptr);
    ASSERT_NE(restored.findLeafForPanel(2), nullptr);
    ASSERT_NE(restored.findLeafForPanel(3), nullptr);
    ASSERT_NE(restored.findLeafForPanel(4), nullptr);
    EXPECT_EQ(restored.findLeafForPanel(4)->selectedPanel, 4u);
    EXPECT_TRUE(restored.validateInvariants());
}

TEST(DockNodeTest, HideTabBarRoundTripsLayoutJson)
{
    FDockTreeModel model;
    registerPanel(model, 1, "viewport");
    ASSERT_TRUE(model.addPanel(1));
    ASSERT_TRUE(model.setHideTabBar(model.getRootNode()->id, true));
    EXPECT_TRUE(model.getRootNode()->bHideTabBar);

    const nlohmann::json layout = model.exportLayoutJson();
    EXPECT_TRUE(layout["root"].value("hideTabBar", false));

    FDockTreeModel restored;
    registerPanel(restored, 1, "viewport");
    ASSERT_TRUE(restored.addPanel(1));
    ASSERT_TRUE(restored.importLayoutJson(layout));
    ASSERT_NE(restored.findLeafForPanel(1), nullptr);
    EXPECT_TRUE(restored.findLeafForPanel(1)->bHideTabBar);
}

TEST(DockNodeTest, FloatingHideTabBarRoundTripsLayoutJson)
{
    FDockContext source;
    source.bAllowFloating = true;
    source.bAllowTearOff  = true;
    const DockPanelId inspectorId = source.addPanel("inspector", "Inspector", std::make_shared<UIPanel>("InspectorBody"));
    ASSERT_NE(inspectorId, kInvalidDockPanelId);
    const FDockFloatingWindowId floatingId = source.tearOffPanel(inspectorId, {180.0f, 140.0f}, {360.0f, 280.0f});
    ASSERT_NE(floatingId, kInvalidFloatingWindowId);
    source.setFloatingHideTabBar(floatingId, true);

    const nlohmann::json layout = source.exportLayoutJson();
    ASSERT_TRUE(layout.contains("floating"));
    ASSERT_EQ(layout["floating"].size(), 1u);
    EXPECT_TRUE(layout["floating"][0].value("hideTabBar", false));

    FDockContext restored;
    restored.bAllowFloating = true;
    restored.bAllowTearOff  = true;
    ASSERT_NE(restored.addPanel("inspector", "Inspector", std::make_shared<UIPanel>("InspectorBody2")), kInvalidDockPanelId);
    ASSERT_TRUE(restored.importLayoutJson(layout));
    ASSERT_EQ(restored.floatingWindows().size(), 1u);
    EXPECT_TRUE(restored.floatingWindows().front().bHideTabBar);
}

TEST(DockNodeTest, ImportRejectsUnknownPanelKey)
{
    FDockTreeModel model;
    registerPanel(model, 1, "viewport");
    ASSERT_TRUE(model.addPanel(1));
    const nlohmann::json layout = {
        {"version", 1},
        {"root",
         {{"kind", "leaf"},
          {"panels", nlohmann::json::array({"missing-panel"})}}},
    };
    EXPECT_FALSE(model.importLayoutJson(layout));
    EXPECT_EQ(model.getRootNode()->panelIds, std::vector<DockPanelId>({1}));
    EXPECT_TRUE(model.validateInvariants());
}

TEST(DockNodeTest, ImportMountsMissingPanelsOnFirstLeaf)
{
    FDockTreeModel model;
    registerPanel(model, 1, "viewport");
    registerPanel(model, 2, "hierarchy");
    ASSERT_TRUE(model.addPanel(1));
    ASSERT_TRUE(model.addPanel(2));
    const nlohmann::json layout = {
        {"version", 1},
        {"root", {{"kind", "leaf"}, {"panels", nlohmann::json::array({"viewport"})}, {"selected", "viewport"}}},
    };
    ASSERT_TRUE(model.importLayoutJson(layout));
    ASSERT_NE(model.findLeafForPanel(1), nullptr);
    ASSERT_NE(model.findLeafForPanel(2), nullptr);
    EXPECT_TRUE(model.validateInvariants());
}

TEST(DockNodeTest, ContextExportImportRestoresFloatingGeometry)
{
    FDockContext source;
    source.bAllowFloating = true;
    source.bAllowTearOff  = true;
    const DockPanelId viewportId = source.addPanel("viewport", "Viewport", std::make_shared<UIPanel>("ViewportBody"));
    const DockPanelId inspectorId = source.addPanel("inspector", "Inspector", std::make_shared<UIPanel>("InspectorBody"));
    const DockPanelId hierarchyId = source.addPanel("hierarchy", "Hierarchy", std::make_shared<UIPanel>("HierarchyBody"));
    ASSERT_NE(viewportId, kInvalidDockPanelId);
    ASSERT_NE(inspectorId, kInvalidDockPanelId);
    ASSERT_NE(hierarchyId, kInvalidDockPanelId);
    ASSERT_NE(source.tearOffPanel(inspectorId, {180.0f, 140.0f}, {360.0f, 280.0f}), kInvalidFloatingWindowId);
    ASSERT_TRUE(source.addPanelToFloating(source.floatingWindows().front().id, hierarchyId));
    source.setFloatingWindowActivePanel(source.floatingWindows().front().id, hierarchyId);
    source.setFloatingWindowRect(source.floatingWindows().front().id, {180.0f, 140.0f}, {360.0f, 280.0f});

    const nlohmann::json layout = source.exportLayoutJson();
    ASSERT_TRUE(layout.contains("floating"));
    ASSERT_EQ(layout["floating"].size(), 1u);

    FDockContext restored;
    restored.bAllowFloating = true;
    restored.bAllowTearOff  = true;
    ASSERT_NE(restored.addPanel("viewport", "Viewport", std::make_shared<UIPanel>("ViewportBody2")), kInvalidDockPanelId);
    ASSERT_NE(restored.addPanel("inspector", "Inspector", std::make_shared<UIPanel>("InspectorBody2")), kInvalidDockPanelId);
    ASSERT_NE(restored.addPanel("hierarchy", "Hierarchy", std::make_shared<UIPanel>("HierarchyBody2")), kInvalidDockPanelId);
    ASSERT_TRUE(restored.importLayoutJson(layout));

    ASSERT_EQ(restored.floatingWindows().size(), 1u);
    const auto& floating = restored.floatingWindows().front();
    EXPECT_EQ(floating.pos, glm::vec2(180.0f, 140.0f));
    EXPECT_EQ(floating.size, glm::vec2(360.0f, 280.0f));
    ASSERT_EQ(floating.panelIds.size(), 2u);
    EXPECT_TRUE(restored.isPanelFloating(restored.dockModel().findPanelByStableKey("inspector")->id));
    EXPECT_TRUE(restored.isPanelFloating(restored.dockModel().findPanelByStableKey("hierarchy")->id));
    EXPECT_FALSE(restored.isPanelFloating(restored.dockModel().findPanelByStableKey("viewport")->id));
    EXPECT_EQ(floating.activePanelId, restored.dockModel().findPanelByStableKey("hierarchy")->id);
    EXPECT_EQ(restored.dockModel().findLeafForPanel(restored.dockModel().findPanelByStableKey("inspector")->id), nullptr);
}

TEST(DockNodeTest, ContextImportRejectsUnknownFloatingPanelKey)
{
    FDockContext context;
    context.bAllowFloating = true;
    const DockPanelId viewportId = context.addPanel("viewport", "Viewport", std::make_shared<UIPanel>("ViewportBody"));
    ASSERT_NE(viewportId, kInvalidDockPanelId);
    const nlohmann::json layout = {
        {"version", 1},
        {"root", {{"kind", "leaf"}, {"panels", nlohmann::json::array({"viewport"})}, {"selected", "viewport"}}},
        {"floating", nlohmann::json::array({nlohmann::json::object({
            {"panels", nlohmann::json::array({"missing-panel"})},
            {"pos", nlohmann::json::array({10.0f, 20.0f})},
            {"size", nlohmann::json::array({100.0f, 80.0f})},
        })})},
    };
    EXPECT_FALSE(context.importLayoutJson(layout));
    EXPECT_TRUE(context.floatingWindows().empty());
    ASSERT_NE(context.dockModel().findLeafForPanel(viewportId), nullptr);
}

TEST(DockNodeTest, ContextImportAcceptsTreeOnlySnapshot)
{
    FDockContext context;
    context.bAllowFloating = true;
    ASSERT_NE(context.addPanel("viewport", "Viewport", std::make_shared<UIPanel>("ViewportBody")), kInvalidDockPanelId);
    ASSERT_NE(context.addPanel("inspector", "Inspector", std::make_shared<UIPanel>("InspectorBody")), kInvalidDockPanelId);
    const nlohmann::json layout = {
        {"version", 1},
        {"root", {{"kind", "leaf"}, {"panels", nlohmann::json::array({"viewport", "inspector"})}, {"selected", "viewport"}}},
    };
    ASSERT_TRUE(context.importLayoutJson(layout));
    EXPECT_TRUE(context.floatingWindows().empty());
    EXPECT_NE(context.dockModel().findLeafForPanel(context.dockModel().findPanelByStableKey("inspector")->id), nullptr);
}

TEST(DockNodeTest, CollectLayoutPanelKeysWalksDockedAndFloating)
{
    FDockContext source;
    source.bAllowFloating = true;
    source.bAllowTearOff = true;
    ASSERT_NE(source.addPanel("viewport", "Viewport", std::make_shared<UIPanel>("V")), kInvalidDockPanelId);
    ASSERT_NE(source.addPanel("inspector", "Inspector", std::make_shared<UIPanel>("I")), kInvalidDockPanelId);
    ASSERT_NE(source.addPanel("hierarchy", "Hierarchy", std::make_shared<UIPanel>("H")), kInvalidDockPanelId);
    const DockPanelId inspectorId = source.findPanelByStableKey("inspector")->id;
    ASSERT_NE(source.tearOffPanel(inspectorId, {10.f, 10.f}, {100.f, 80.f}), kInvalidFloatingWindowId);

    const std::vector<std::string> keys = FDockContext::collectLayoutPanelKeys(source.exportLayoutJson());
    EXPECT_EQ(keys.size(), 3u);
    EXPECT_NE(std::find(keys.begin(), keys.end(), "viewport"), keys.end());
    EXPECT_NE(std::find(keys.begin(), keys.end(), "inspector"), keys.end());
    EXPECT_NE(std::find(keys.begin(), keys.end(), "hierarchy"), keys.end());
}

TEST(DockNodeTest, HasPanelAndActivatePanelSelectByStableKey)
{
    FDockContext context;
    ASSERT_NE(context.addPanel("viewport", "Viewport", std::make_shared<UIPanel>("V")), kInvalidDockPanelId);
    ASSERT_NE(context.addPanel("inspector", "Inspector", std::make_shared<UIPanel>("I")), kInvalidDockPanelId);
    EXPECT_TRUE(context.hasPanel("viewport"));
    EXPECT_FALSE(context.hasPanel("missing"));

    ASSERT_TRUE(context.activatePanel("viewport"));
    const FDockContext::FPanel* viewport = context.findPanelByStableKey("viewport");
    ASSERT_NE(viewport, nullptr);
    const FDockNode* leaf = context.dockModel().findLeafForPanel(viewport->id);
    ASSERT_NE(leaf, nullptr);
    EXPECT_EQ(leaf->selectedPanel, viewport->id);

    ASSERT_TRUE(context.activatePanel("inspector"));
    const FDockContext::FPanel* inspector = context.findPanelByStableKey("inspector");
    ASSERT_NE(inspector, nullptr);
    EXPECT_EQ(context.dockModel().findLeafForPanel(inspector->id)->selectedPanel, inspector->id);
    ASSERT_TRUE(context.activatePanel("inspector"));
    EXPECT_EQ(context.findPanelByStableKey("inspector"), inspector);
}

TEST(DockNodeTest, ActivatePanelGraftsSelectedTabAndDetachedStopsTick)
{
    struct TickProbe final : public UIPanel
    {
        explicit TickProbe(std::string name) : UIPanel(std::move(name)) {}
        int ticks = 0;
        [[nodiscard]] bool wantsTick() const override { return true; }
        void tick(float) override { ++ticks; }
    };

    WidgetTree tree({.width = 800, .height = 600});
    auto context = std::make_shared<FDockContext>();
    auto visible = std::make_shared<TickProbe>("VisibleTab");
    auto hidden = std::make_shared<TickProbe>("HiddenTab");
    ASSERT_NE(context->addPanel("visible", "Visible", visible), kInvalidDockPanelId);
    ASSERT_NE(context->addPanel("hidden", "Hidden", hidden), kInvalidDockPanelId);

    auto dock = std::make_shared<UIDockSpace>("Dock");
    dock->setContext(context);
    FCanvasSlotArgs fill;
    fill.anchorMin = {0.0f, 0.0f};
    fill.anchorMax = {1.0f, 1.0f};
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), dock, fill).valid());
    (void)tree.buildSnapshot(UIFrameBuildContext{});

    ASSERT_TRUE(context->activatePanel("visible"));
    (void)tree.buildSnapshot(UIFrameBuildContext{});
    visible->ticks = 0;
    hidden->ticks = 0;
    tree.tick(1.0f / 60.0f);
    EXPECT_EQ(visible->ticks, 1);
    EXPECT_EQ(hidden->ticks, 0);

    ASSERT_TRUE(context->activatePanel("hidden"));
    (void)tree.buildSnapshot(UIFrameBuildContext{});
    visible->ticks = 0;
    hidden->ticks = 0;
    tree.tick(1.0f / 60.0f);
    EXPECT_EQ(visible->ticks, 0);
    EXPECT_EQ(hidden->ticks, 1);
}

TEST(DockNodeTest, SanitizeLayoutJsonDropsUnknownDockedAndFloatingKeys)
{
    const nlohmann::json layout = {
        {"version", 1},
        {"root",
         {{"kind", "split"},
          {"orientation", "vertical"},
          {"ratio", 0.5f},
          {"children",
           nlohmann::json::array({
               nlohmann::json{{"kind", "leaf"},
                              {"panels", nlohmann::json::array({"viewport", "gui-workbench"})},
                              {"selected", "gui-workbench"}},
               nlohmann::json{{"kind", "leaf"},
                              {"panels", nlohmann::json::array({"inspector"})},
                              {"selected", "inspector"}},
           })}}},
        {"floating",
         nlohmann::json::array({
             nlohmann::json{{"panels", nlohmann::json::array({"missing-panel"})},
                            {"pos", nlohmann::json::array({10.0f, 20.0f})},
                            {"size", nlohmann::json::array({100.0f, 80.0f})}},
             nlohmann::json{{"panels", nlohmann::json::array({"hierarchy", "gone"})},
                            {"selected", "gone"},
                            {"pos", nlohmann::json::array({30.0f, 40.0f})},
                            {"size", nlohmann::json::array({120.0f, 90.0f})}},
         })},
    };

    const nlohmann::json sanitized = FDockContext::sanitizeLayoutJson(
        layout, std::unordered_set<std::string>{"viewport", "inspector", "hierarchy"});

    EXPECT_EQ(sanitized["root"]["children"][0]["panels"], nlohmann::json::array({"viewport"}));
    EXPECT_EQ(sanitized["root"]["children"][0]["selected"], "viewport");
    EXPECT_EQ(sanitized["root"]["children"][1]["panels"], nlohmann::json::array({"inspector"}));
    ASSERT_EQ(sanitized["floating"].size(), 1u);
    EXPECT_EQ(sanitized["floating"][0]["panels"], nlohmann::json::array({"hierarchy"}));
    EXPECT_EQ(sanitized["floating"][0]["selected"], "hierarchy");

    FDockContext context;
    context.bAllowFloating = true;
    context.bAllowTearOff  = true;
    ASSERT_NE(context.addPanel("viewport", "Viewport", std::make_shared<UIPanel>("V")), kInvalidDockPanelId);
    ASSERT_NE(context.addPanel("inspector", "Inspector", std::make_shared<UIPanel>("I")), kInvalidDockPanelId);
    ASSERT_NE(context.addPanel("hierarchy", "Hierarchy", std::make_shared<UIPanel>("H")), kInvalidDockPanelId);
    ASSERT_TRUE(context.importLayoutJson(sanitized));
    EXPECT_NE(context.dockModel().findLeafForPanel(context.findPanelByStableKey("viewport")->id), nullptr);
    EXPECT_NE(context.dockModel().findLeafForPanel(context.findPanelByStableKey("inspector")->id), nullptr);
    ASSERT_EQ(context.floatingWindows().size(), 1u);
    EXPECT_EQ(context.floatingWindows().front().panelIds.size(), 1u);
}

TEST(DockNodeTest, ClosePanelByStableKeyRemovesRegistryRecord)
{
    FDockContext context;
    ASSERT_NE(context.addPanel("viewport", "Viewport", std::make_shared<UIPanel>("V")), kInvalidDockPanelId);
    ASSERT_NE(context.addPanel("inspector", "Inspector", std::make_shared<UIPanel>("I")), kInvalidDockPanelId);
    ASSERT_TRUE(context.closePanel("inspector"));
    EXPECT_FALSE(context.hasPanel("inspector"));
    EXPECT_TRUE(context.hasPanel("viewport"));
}

TEST(DockNodeTest, AddPanelAfterSplitUsesFirstLeaf)
{
    FDockTreeModel model;
    registerPanel(model, 1, "viewport");
    registerPanel(model, 2, "inspector");
    registerPanel(model, 3, "stats");
    ASSERT_TRUE(model.addPanel(1));
    ASSERT_TRUE(model.splitLeaf(model.getRootNode()->id, EDockCardinalSide::East, 2, 0.5f));
    ASSERT_TRUE(model.addPanel(3));
    const FDockNode* viewportLeaf = model.findLeafForPanel(1);
    ASSERT_NE(viewportLeaf, nullptr);
    EXPECT_NE(std::find(viewportLeaf->panelIds.begin(), viewportLeaf->panelIds.end(), DockPanelId{3}),
              viewportLeaf->panelIds.end());
}

TEST(DockNodeTest, NewPanelDocksOnLastFocusedLeaf)
{
    FDockContext context;
    const DockPanelId viewportId = context.addPanel("viewport", "Viewport", std::make_shared<UIPanel>("V"));
    const DockPanelId inspectorId = context.addPanel("inspector", "Inspector", std::make_shared<UIPanel>("I"));
    ASSERT_NE(viewportId, kInvalidDockPanelId);
    ASSERT_NE(inspectorId, kInvalidDockPanelId);
    ASSERT_TRUE(context.dockModel().splitLeaf(context.dockModel().getRootNode()->id, EDockCardinalSide::East, inspectorId, 0.5f));
    ASSERT_TRUE(context.activatePanel("inspector"));
    const DockNodeId inspectorLeaf = context.lastFocusedLeafId();
    EXPECT_EQ(inspectorLeaf, context.dockModel().findLeafForPanel(inspectorId)->id);

    const DockPanelId statsId = context.addPanel("stats", "Stats", std::make_shared<UIPanel>("S"));
    ASSERT_NE(statsId, kInvalidDockPanelId);
    EXPECT_EQ(context.dockModel().findLeafForPanel(statsId)->id, inspectorLeaf);
}

} // namespace ya
