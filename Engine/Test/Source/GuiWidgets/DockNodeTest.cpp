#include "GUI/Widgets/Controls/DockSpace/DockNode.h"
#include "GUI/Widgets/Controls/DockSpace/DockContext.h"
#include "GUI/Widgets/Controls/DockSpace/DockDropTarget.h"
#include "GUI/Widgets/Controls/DockSpace/DockFloatingHost.h"
#include "GUI/Widgets/Controls/DockSpace/DockFloatingWindow.h"
#include "GUI/Widgets/Controls/DockSpace/DockSpace.h"
#include "GUI/Widgets/Controls/DockSpace/DockTabStack.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/WidgetTree.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <format>
#include <string_view>
#include <unordered_set>

namespace ya
{

namespace
{
void registerPanel(FDockTreeModel& model, DockPanelId id, const char* key)
{
    ASSERT_TRUE(model.registerPanel({.id = id, .stableKey = key, .title = key}));
}

UIElement* findNamedDescendant(UIElement& root, std::string_view name)
{
    if (root._name == name) {
        return &root;
    }
    for (const UIElementRef& child : root.getChildren()) {
        if (!child) {
            continue;
        }
        if (UIElement* found = findNamedDescendant(*child, name)) {
            return found;
        }
    }
    return nullptr;
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
    EXPECT_EQ(model.getRootNode()->kind, EDockNodeKind::Stack);
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
    EXPECT_EQ(model.getRootNode()->kind, EDockNodeKind::Stack);
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
    ASSERT_EQ(model.getRootNode()->kind, EDockNodeKind::Stack);
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
    EXPECT_EQ(layout["version"], 2);
    EXPECT_TRUE(layout["dockSpace"]["1"].value("hideTabBar", false));

    FDockTreeModel restored;
    registerPanel(restored, 1, "viewport");
    ASSERT_TRUE(restored.addPanel(1));
    ASSERT_TRUE(restored.importLayoutJson(layout));
    ASSERT_NE(restored.findLeafForPanel(1), nullptr);
    EXPECT_TRUE(restored.findLeafForPanel(1)->bHideTabBar);
}

TEST(DockNodeTest, HideTabBarRejectedWhenStackHasMultipleTabs)
{
    FDockTreeModel model;
    registerPanel(model, 1, "viewport");
    registerPanel(model, 2, "inspector");
    ASSERT_TRUE(model.addPanel(1));
    ASSERT_TRUE(model.addPanel(2));
    EXPECT_FALSE(model.setHideTabBar(model.getRootNode()->id, true));
    EXPECT_FALSE(model.getRootNode()->bHideTabBar);
}

TEST(DockNodeTest, AddingSecondPanelClearsHiddenTabBar)
{
    FDockTreeModel model;
    registerPanel(model, 1, "viewport");
    registerPanel(model, 2, "inspector");
    ASSERT_TRUE(model.addPanel(1));
    ASSERT_TRUE(model.setHideTabBar(model.getRootNode()->id, true));
    EXPECT_TRUE(model.getRootNode()->bHideTabBar);
    ASSERT_TRUE(model.addPanel(2));
    EXPECT_FALSE(model.getRootNode()->bHideTabBar);
}

TEST(DockNodeTest, PageRoleMayHideTabBarWithMultipleTabs)
{
    FDockTreeModel model;
    registerPanel(model, 1, "level-editor");
    registerPanel(model, 2, "ui-editor");
    ASSERT_TRUE(model.addPanel(1));
    ASSERT_TRUE(model.addPanel(2));
    ASSERT_TRUE(model.setLeafRole(model.getRootNode()->id, EDockLeafRole::Page));
    ASSERT_TRUE(model.setHideTabBar(model.getRootNode()->id, true));
    EXPECT_TRUE(model.getRootNode()->bHideTabBar);
}

TEST(DockNodeTest, LeafRoleRoundTripsLayoutJson)
{
    FDockTreeModel model;
    registerPanel(model, 1, "level-editor");
    ASSERT_TRUE(model.addPanel(1));
    ASSERT_TRUE(model.setLeafRole(model.getRootNode()->id, EDockLeafRole::Page));
    ASSERT_TRUE(model.setHideTabBar(model.getRootNode()->id, true));

    const nlohmann::json layout = model.exportLayoutJson();
    EXPECT_EQ(layout["dockSpace"]["1"].value("role", ""), "page");
    EXPECT_TRUE(layout["dockSpace"]["1"].value("hideTabBar", false));

    FDockTreeModel restored;
    registerPanel(restored, 1, "level-editor");
    ASSERT_TRUE(restored.addPanel(1));
    ASSERT_TRUE(restored.importLayoutJson(layout));
    ASSERT_NE(restored.findLeafForPanel(1), nullptr);
    EXPECT_EQ(restored.findLeafForPanel(1)->leafRole, EDockLeafRole::Page);
    EXPECT_TRUE(restored.findLeafForPanel(1)->bHideTabBar);
    EXPECT_EQ(restored.findFirstLeafWithRole(EDockLeafRole::Page), restored.findLeafForPanel(1)->id);
}

TEST(DockNodeTest, FloatingHideTabBarRoundTripsLayoutJson)
{
    FDockContext source;
    source.bAllowFloating = true;
    source.bAllowTearOff  = true;
    const DockPanelId inspectorId = source.addPanel("inspector", "Inspector", std::make_shared<UICanvasPanel>("InspectorBody"));
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
    ASSERT_NE(restored.addPanel("inspector", "Inspector", std::make_shared<UICanvasPanel>("InspectorBody2")), kInvalidDockPanelId);
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
        {"version", 2},
        {"tree", {{"kind", "leaf"}, {"id", "main"}}},
        {"dockSpace",
         {{"main", {{"panels", nlohmann::json::array({"missing-panel"})}}}}},
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
        {"version", 2},
        {"tree", {{"kind", "leaf"}, {"id", "main"}}},
        {"dockSpace", {{"main", {{"panels", nlohmann::json::array({"viewport"})}, {"selected", "viewport"}}}}},
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
    const DockPanelId viewportId = source.addPanel("viewport", "Viewport", std::make_shared<UICanvasPanel>("ViewportBody"));
    const DockPanelId inspectorId = source.addPanel("inspector", "Inspector", std::make_shared<UICanvasPanel>("InspectorBody"));
    const DockPanelId hierarchyId = source.addPanel("hierarchy", "Hierarchy", std::make_shared<UICanvasPanel>("HierarchyBody"));
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
    ASSERT_NE(restored.addPanel("viewport", "Viewport", std::make_shared<UICanvasPanel>("ViewportBody2")), kInvalidDockPanelId);
    ASSERT_NE(restored.addPanel("inspector", "Inspector", std::make_shared<UICanvasPanel>("InspectorBody2")), kInvalidDockPanelId);
    ASSERT_NE(restored.addPanel("hierarchy", "Hierarchy", std::make_shared<UICanvasPanel>("HierarchyBody2")), kInvalidDockPanelId);
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
    const DockPanelId viewportId = context.addPanel("viewport", "Viewport", std::make_shared<UICanvasPanel>("ViewportBody"));
    ASSERT_NE(viewportId, kInvalidDockPanelId);
    const nlohmann::json layout = {
        {"version", 2},
        {"tree", {{"kind", "leaf"}, {"id", "main"}}},
        {"dockSpace", {{"main", {{"panels", nlohmann::json::array({"viewport"})}, {"selected", "viewport"}}}}},
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
    ASSERT_NE(context.addPanel("viewport", "Viewport", std::make_shared<UICanvasPanel>("ViewportBody")), kInvalidDockPanelId);
    ASSERT_NE(context.addPanel("inspector", "Inspector", std::make_shared<UICanvasPanel>("InspectorBody")), kInvalidDockPanelId);
    const nlohmann::json layout = {
        {"version", 2},
        {"tree", {{"kind", "leaf"}, {"id", "main"}}},
        {"dockSpace", {{"main", {{"panels", nlohmann::json::array({"viewport", "inspector"})}, {"selected", "viewport"}}}}},
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
    ASSERT_NE(source.addPanel("viewport", "Viewport", std::make_shared<UICanvasPanel>("V")), kInvalidDockPanelId);
    ASSERT_NE(source.addPanel("inspector", "Inspector", std::make_shared<UICanvasPanel>("I")), kInvalidDockPanelId);
    ASSERT_NE(source.addPanel("hierarchy", "Hierarchy", std::make_shared<UICanvasPanel>("H")), kInvalidDockPanelId);
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
    ASSERT_NE(context.addPanel("viewport", "Viewport", std::make_shared<UICanvasPanel>("V")), kInvalidDockPanelId);
    ASSERT_NE(context.addPanel("inspector", "Inspector", std::make_shared<UICanvasPanel>("I")), kInvalidDockPanelId);
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
    struct TickProbe final : public UICanvasPanel
    {
        explicit TickProbe(std::string name) : UICanvasPanel(std::move(name)) { enableTick(); }
        int ticks = 0;
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
        {"version", 2},
        {"tree",
         {{"kind", "split"},
          {"orientation", "vertical"},
          {"ratio", 0.5f},
          {"children",
           nlohmann::json::array({
               nlohmann::json{{"kind", "leaf"}, {"id", "left"}},
               nlohmann::json{{"kind", "leaf"}, {"id", "right"}},
           })}}},
        {"dockSpace",
         {
             {"left",
              {{"panels", nlohmann::json::array({"viewport", "gui-workbench"})},
               {"selected", "gui-workbench"}}},
             {"right",
              {{"panels", nlohmann::json::array({"inspector"})},
               {"selected", "inspector"}}},
         }},
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

    EXPECT_EQ(sanitized["dockSpace"]["left"]["panels"], nlohmann::json::array({"viewport"}));
    EXPECT_EQ(sanitized["dockSpace"]["left"]["selected"], "viewport");
    EXPECT_EQ(sanitized["dockSpace"]["right"]["panels"], nlohmann::json::array({"inspector"}));
    ASSERT_EQ(sanitized["floating"].size(), 1u);
    EXPECT_EQ(sanitized["floating"][0]["panels"], nlohmann::json::array({"hierarchy"}));
    EXPECT_EQ(sanitized["floating"][0]["selected"], "hierarchy");

    FDockContext context;
    context.bAllowFloating = true;
    context.bAllowTearOff  = true;
    ASSERT_NE(context.addPanel("viewport", "Viewport", std::make_shared<UICanvasPanel>("V")), kInvalidDockPanelId);
    ASSERT_NE(context.addPanel("inspector", "Inspector", std::make_shared<UICanvasPanel>("I")), kInvalidDockPanelId);
    ASSERT_NE(context.addPanel("hierarchy", "Hierarchy", std::make_shared<UICanvasPanel>("H")), kInvalidDockPanelId);
    ASSERT_TRUE(context.importLayoutJson(sanitized));
    EXPECT_NE(context.dockModel().findLeafForPanel(context.findPanelByStableKey("viewport")->id), nullptr);
    EXPECT_NE(context.dockModel().findLeafForPanel(context.findPanelByStableKey("inspector")->id), nullptr);
    ASSERT_EQ(context.floatingWindows().size(), 1u);
    EXPECT_EQ(context.floatingWindows().front().panelIds.size(), 1u);
}

TEST(DockNodeTest, ClosePanelByStableKeyRemovesRegistryRecord)
{
    FDockContext context;
    ASSERT_NE(context.addPanel("viewport", "Viewport", std::make_shared<UICanvasPanel>("V")), kInvalidDockPanelId);
    ASSERT_NE(context.addPanel("inspector", "Inspector", std::make_shared<UICanvasPanel>("I")), kInvalidDockPanelId);
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
    const DockPanelId viewportId = context.addPanel("viewport", "Viewport", std::make_shared<UICanvasPanel>("V"));
    const DockPanelId inspectorId = context.addPanel("inspector", "Inspector", std::make_shared<UICanvasPanel>("I"));
    ASSERT_NE(viewportId, kInvalidDockPanelId);
    ASSERT_NE(inspectorId, kInvalidDockPanelId);
    ASSERT_TRUE(context.dockModel().splitLeaf(context.dockModel().getRootNode()->id, EDockCardinalSide::East, inspectorId, 0.5f));
    ASSERT_TRUE(context.activatePanel("inspector"));
    const DockNodeId inspectorLeaf = context.lastFocusedLeafId();
    EXPECT_EQ(inspectorLeaf, context.dockModel().findLeafForPanel(inspectorId)->id);

    const DockPanelId statsId = context.addPanel("stats", "Stats", std::make_shared<UICanvasPanel>("S"));
    ASSERT_NE(statsId, kInvalidDockPanelId);
    EXPECT_EQ(context.dockModel().findLeafForPanel(statsId)->id, inspectorLeaf);
}

TEST(DockNodeTest, AddPanelAfterProjectionSyncsFocusedStackWithoutRematerialize)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto context = std::make_shared<FDockContext>();
    auto dock = std::make_shared<UIDockSpace>("Dock");
    dock->setContext(context);
    FCanvasSlotArgs fill;
    fill.anchorMin = {0.0f, 0.0f};
    fill.anchorMax = {1.0f, 1.0f};
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), dock, fill).valid());

    const DockPanelId sceneId = context->addPanel("scene", "Scene", std::make_shared<UICanvasPanel>("Scene"));
    const DockPanelId inspectorId =
        context->addPanel("inspector", "Inspector", std::make_shared<UICanvasPanel>("Inspector"));
    ASSERT_NE(sceneId, kInvalidDockPanelId);
    ASSERT_NE(inspectorId, kInvalidDockPanelId);
    (void)tree.buildSnapshot(UIFrameBuildContext{});

    ASSERT_TRUE(context->dockModel().splitLeaf(context->dockModel().getRootNode()->id,
                                               EDockCardinalSide::East,
                                               inspectorId));
    context->fireDockUpdated();
    ASSERT_TRUE(context->activatePanel("inspector"));

    const DockPanelId statsId = context->addPanel("stats", "Stats", std::make_shared<UICanvasPanel>("Stats"));
    ASSERT_NE(statsId, kInvalidDockPanelId);

    const FDockNode* inspectorLeaf = context->dockModel().findLeafForPanel(inspectorId);
    ASSERT_NE(inspectorLeaf, nullptr);
    auto* inspectorBar = dynamic_cast<UIDockTabWell*>(
        findNamedDescendant(*dock, std::format("DockTabBar{}", inspectorLeaf->id)));
    ASSERT_NE(inspectorBar, nullptr);
    EXPECT_EQ(inspectorBar->getChildren().size(), 2u);
    EXPECT_NE(findNamedDescendant(*dock, "Tab_Stats"), nullptr);
    EXPECT_EQ(context->dockModel().findLeafForPanel(statsId)->id, inspectorLeaf->id);
}

TEST(DockNodeTest, TearOffCopiesHostAndPanelIdentityIntoPlacement)
{
    FDockContext context;
    context.bAllowFloating = true;
    context.bAllowTearOff  = true;
    context.sourceScope    = EDockSourceScope::EditorOwned;
    context.hostWindowId   = 7;
    const DockPanelId inspectorId = context.addPanel("inspector", "Inspector", std::make_shared<UICanvasPanel>("I"));
    ASSERT_NE(inspectorId, kInvalidDockPanelId);
    ASSERT_TRUE(context.setPanelIdentity(inspectorId, 2, "panel-a"));

    const FDockFloatingWindowId floatingId =
        context.tearOffPanel(inspectorId, {10.0f, 20.0f}, {200.0f, 100.0f});
    ASSERT_NE(floatingId, kInvalidFloatingWindowId);
    const FDockContext::FDockFloatingPlacement* placement = context.findFloatingById(floatingId);
    ASSERT_NE(placement, nullptr);
    EXPECT_EQ(placement->projection, EDockFloatingProjection::InProcessOverlay);
    EXPECT_EQ(placement->sourceScope, EDockSourceScope::EditorOwned);
    EXPECT_EQ(placement->targetWindowId, 7u);
    EXPECT_EQ(placement->ownerEditorId, 2u);
    EXPECT_EQ(placement->documentKey, "panel-a");
    EXPECT_EQ(placement->geometrySpace, EDockGeometrySpace::TreeLocal);

    const nlohmann::json layout = context.exportLayoutJson();
    ASSERT_EQ(layout["floating"].size(), 1u);
    EXPECT_TRUE(layout["windows"].empty());
    EXPECT_EQ(layout["floating"][0]["projection"], "inProcessOverlay");
    EXPECT_EQ(layout["floating"][0]["geometrySpace"], "treeLocal");
    EXPECT_EQ(layout["floating"][0]["sourceScope"], "editorOwned");
    EXPECT_EQ(layout["floating"][0]["targetWindowId"], 7);
    EXPECT_EQ(layout["floating"][0]["ownerEditorId"], 2);
    EXPECT_EQ(layout["floating"][0]["documentKey"], "panel-a");
}

TEST(DockNodeTest, FloatingWithoutProjectionImportsAsInProcessOverlay)
{
    FDockContext restored;
    restored.bAllowFloating = true;
    restored.bAllowTearOff  = true;
    restored.hostWindowId   = 3;
    ASSERT_NE(restored.addPanel("viewport", "Viewport", std::make_shared<UICanvasPanel>("V")), kInvalidDockPanelId);
    ASSERT_NE(restored.addPanel("inspector", "Inspector", std::make_shared<UICanvasPanel>("I")), kInvalidDockPanelId);
    const nlohmann::json layout = {
        {"version", 2},
        {"tree", {{"kind", "leaf"}, {"id", "main"}}},
        {"dockSpace", {{"main", {{"panels", nlohmann::json::array({"viewport"})}, {"selected", "viewport"}}}}},
        {"floating",
         nlohmann::json::array({nlohmann::json{{"panels", nlohmann::json::array({"inspector"})},
                                               {"pos", nlohmann::json::array({12.0f, 24.0f})},
                                               {"size", nlohmann::json::array({320.0f, 240.0f})}}})},
    };
    ASSERT_TRUE(restored.importLayoutJson(layout));
    ASSERT_EQ(restored.floatingPlacements().size(), 1u);
    const auto& placement = restored.floatingPlacements().front();
    EXPECT_EQ(placement.projection, EDockFloatingProjection::InProcessOverlay);
    EXPECT_EQ(placement.geometrySpace, EDockGeometrySpace::TreeLocal);
    EXPECT_EQ(placement.sourceScope, EDockSourceScope::WindowRoot);
    EXPECT_EQ(placement.targetWindowId, 3u);
    EXPECT_EQ(placement.pos, glm::vec2(12.0f, 24.0f));
    const nlohmann::json exported = restored.exportLayoutJson();
    ASSERT_EQ(exported["floating"].size(), 1u);
    EXPECT_TRUE(exported["windows"].empty());
    EXPECT_EQ(exported["floating"][0]["pos"], nlohmann::json::array({12.0f, 24.0f}));
    EXPECT_EQ(exported["floating"][0]["geometrySpace"], "treeLocal");
    EXPECT_NE(exported["floating"][0]["geometrySpace"], "screen");
}

TEST(DockNodeTest, OverlayHostSkipsNativeWindowPlacement)
{
    auto context = std::make_shared<FDockContext>();
    context->bAllowFloating = true;
    context->bAllowTearOff  = true;
    const DockPanelId overlayId = context->addPanel("overlay", "Overlay", std::make_shared<UICanvasPanel>("O"));
    const DockPanelId nativeId  = context->addPanel("native", "Native", std::make_shared<UICanvasPanel>("N"));
    ASSERT_NE(overlayId, kInvalidDockPanelId);
    ASSERT_NE(nativeId, kInvalidDockPanelId);

    auto host = std::make_shared<UIDockFloatingHost>("Host");
    host->bindContext(context);
    ASSERT_NE(context->tearOffPanel(overlayId, {0.0f, 0.0f}, {120.0f, 80.0f}), kInvalidFloatingWindowId);
    context->fireFloatingUpdated();
    EXPECT_EQ(host->overlayWindowCount(), 1u);

    const FDockFloatingWindowId nativeFloating =
        context->tearOffPanel(nativeId, {40.0f, 40.0f}, {120.0f, 80.0f}, EDockFloatingProjection::NativeWindow);
    ASSERT_NE(nativeFloating, kInvalidFloatingWindowId);
    EXPECT_EQ(context->findFloatingById(nativeFloating)->projection, EDockFloatingProjection::NativeWindow);
    EXPECT_EQ(context->findFloatingById(nativeFloating)->targetWindowId, 0u);
    context->fireFloatingUpdated();
    EXPECT_EQ(context->floatingPlacements().size(), 2u);
    EXPECT_EQ(host->overlayWindowCount(), 1u);

    ASSERT_TRUE(context->setFloatingProjection(nativeFloating, EDockFloatingProjection::InProcessOverlay));
    EXPECT_EQ(host->overlayWindowCount(), 2u);
}

TEST(DockNodeTest, NativeWindowTearOffLeavesTargetUnbound)
{
    FDockContext context;
    context.bAllowFloating = true;
    context.bAllowTearOff  = true;
    context.hostWindowId   = 7;
    const DockPanelId inspectorId = context.addPanel("inspector", "Inspector", std::make_shared<UICanvasPanel>("I"));
    ASSERT_NE(inspectorId, kInvalidDockPanelId);

    const FDockFloatingWindowId floatingId =
        context.tearOffPanel(inspectorId, {10.0f, 20.0f}, {200.0f, 100.0f}, EDockFloatingProjection::NativeWindow);
    ASSERT_NE(floatingId, kInvalidFloatingWindowId);
    const FDockContext::FDockFloatingPlacement* placement = context.findFloatingById(floatingId);
    ASSERT_NE(placement, nullptr);
    EXPECT_EQ(placement->projection, EDockFloatingProjection::NativeWindow);
    EXPECT_EQ(placement->targetWindowId, 0u);
    EXPECT_FALSE(context.bindFloatingTargetWindow(floatingId, 0));
    EXPECT_TRUE(context.bindFloatingTargetWindow(floatingId, 42));
    EXPECT_EQ(context.findFloatingById(floatingId)->targetWindowId, 42u);

    const DockPanelId overlayId = context.addPanel("overlay", "Overlay", std::make_shared<UICanvasPanel>("O"));
    const FDockFloatingWindowId overlayFloating =
        context.tearOffPanel(overlayId, {0.0f, 0.0f}, {80.0f, 80.0f});
    ASSERT_NE(overlayFloating, kInvalidFloatingWindowId);
    EXPECT_EQ(context.findFloatingById(overlayFloating)->targetWindowId, 7u);
    EXPECT_FALSE(context.bindFloatingTargetWindow(overlayFloating, 9));
    EXPECT_EQ(context.findFloatingById(overlayFloating)->targetWindowId, 7u);
}

TEST(DockNodeTest, NativeWindowJsonWithoutTargetStaysUnbound)
{
    FDockContext restored;
    restored.bAllowFloating = true;
    restored.bAllowTearOff  = true;
    restored.hostWindowId   = 3;
    ASSERT_NE(restored.addPanel("viewport", "Viewport", std::make_shared<UICanvasPanel>("V")), kInvalidDockPanelId);
    ASSERT_NE(restored.addPanel("inspector", "Inspector", std::make_shared<UICanvasPanel>("I")), kInvalidDockPanelId);
    const nlohmann::json layout = {
        {"version", 2},
        {"tree", {{"kind", "leaf"}, {"id", "main"}}},
        {"dockSpace", {{"main", {{"panels", nlohmann::json::array({"viewport"})}, {"selected", "viewport"}}}}},
        {"floating",
         nlohmann::json::array({nlohmann::json{{"panels", nlohmann::json::array({"inspector"})},
                                               {"projection", "nativeWindow"},
                                               {"pos", nlohmann::json::array({12.0f, 24.0f})},
                                               {"size", nlohmann::json::array({320.0f, 240.0f})}}})},
    };
    ASSERT_TRUE(restored.importLayoutJson(layout));
    ASSERT_EQ(restored.floatingPlacements().size(), 1u);
    const auto& placement = restored.floatingPlacements().front();
    EXPECT_EQ(placement.projection, EDockFloatingProjection::NativeWindow);
    EXPECT_EQ(placement.geometrySpace, EDockGeometrySpace::TreeLocal);
    EXPECT_EQ(placement.targetWindowId, 0u);
    const nlohmann::json exported = restored.exportLayoutJson();
    EXPECT_TRUE(exported["floating"].empty());
    ASSERT_EQ(exported["windows"].size(), 1u);
    EXPECT_EQ(exported["windows"][0]["projection"], "nativeWindow");
    EXPECT_EQ(exported["windows"][0]["geometrySpace"], "treeLocal");
    EXPECT_EQ(exported["windows"][0]["pos"], nlohmann::json::array({12.0f, 24.0f}));
}

TEST(DockNodeTest, NativeWindowsArrayImportsWithoutTreatingPosAsScreen)
{
    FDockContext restored;
    restored.bAllowFloating = true;
    restored.bAllowTearOff  = true;
    restored.hostWindowId   = 3;
    ASSERT_NE(restored.addPanel("viewport", "Viewport", std::make_shared<UICanvasPanel>("V")), kInvalidDockPanelId);
    ASSERT_NE(restored.addPanel("inspector", "Inspector", std::make_shared<UICanvasPanel>("I")), kInvalidDockPanelId);
    const nlohmann::json layout = {
        {"version", 2},
        {"tree", {{"kind", "leaf"}, {"id", "main"}}},
        {"dockSpace", {{"main", {{"panels", nlohmann::json::array({"viewport"})}, {"selected", "viewport"}}}}},
        {"floating", nlohmann::json::array()},
        {"windows",
         nlohmann::json::array({nlohmann::json{{"panels", nlohmann::json::array({"inspector"})},
                                               {"pos", nlohmann::json::array({12.0f, 24.0f})},
                                               {"size", nlohmann::json::array({320.0f, 240.0f})}}})},
    };
    ASSERT_TRUE(restored.importLayoutJson(layout));
    ASSERT_EQ(restored.floatingPlacements().size(), 1u);
    const auto& placement = restored.floatingPlacements().front();
    EXPECT_EQ(placement.projection, EDockFloatingProjection::NativeWindow);
    EXPECT_EQ(placement.geometrySpace, EDockGeometrySpace::TreeLocal);
    EXPECT_EQ(placement.pos, glm::vec2(12.0f, 24.0f));
}

TEST(DockNodeTest, OverlayExportNeverCopiesPosIntoWindows)
{
    FDockContext context;
    context.bAllowFloating = true;
    context.bAllowTearOff  = true;
    ASSERT_NE(context.addPanel("viewport", "Viewport", std::make_shared<UICanvasPanel>("V")), kInvalidDockPanelId);
    const DockPanelId inspectorId = context.addPanel("inspector", "Inspector", std::make_shared<UICanvasPanel>("I"));
    ASSERT_NE(inspectorId, kInvalidDockPanelId);
    ASSERT_NE(context.tearOffPanel(inspectorId, {99.0f, 88.0f}, {200.0f, 100.0f}), kInvalidFloatingWindowId);

    const nlohmann::json layout = context.exportLayoutJson();
    ASSERT_EQ(layout["floating"].size(), 1u);
    EXPECT_TRUE(layout["windows"].empty());
    EXPECT_EQ(layout["floating"][0]["pos"], nlohmann::json::array({99.0f, 88.0f}));
    EXPECT_EQ(layout["floating"][0]["geometrySpace"], "treeLocal");
}

TEST(DockNodeTest, DuplicatePanelKeysAcrossFloatingAndWindowsFailImport)
{
    FDockContext context;
    context.bAllowFloating = true;
    context.bAllowTearOff  = true;
    ASSERT_NE(context.addPanel("viewport", "Viewport", std::make_shared<UICanvasPanel>("V")), kInvalidDockPanelId);
    ASSERT_NE(context.addPanel("inspector", "Inspector", std::make_shared<UICanvasPanel>("I")), kInvalidDockPanelId);
    const nlohmann::json layout = {
        {"version", 2},
        {"tree", {{"kind", "leaf"}, {"id", "main"}}},
        {"dockSpace", {{"main", {{"panels", nlohmann::json::array({"viewport"})}, {"selected", "viewport"}}}}},
        {"floating",
         nlohmann::json::array({nlohmann::json{{"panels", nlohmann::json::array({"inspector"})},
                                               {"pos", nlohmann::json::array({1.0f, 2.0f})},
                                               {"size", nlohmann::json::array({100.0f, 80.0f})}}})},
        {"windows",
         nlohmann::json::array({nlohmann::json{{"panels", nlohmann::json::array({"inspector"})},
                                               {"pos", nlohmann::json::array({3.0f, 4.0f})},
                                               {"size", nlohmann::json::array({100.0f, 80.0f})}}})},
    };
    EXPECT_FALSE(context.importLayoutJson(layout));
}

TEST(DockNodeTest, OverlayRejectsScreenGeometrySpace)
{
    FDockContext context;
    context.bAllowFloating = true;
    context.bAllowTearOff  = true;
    const DockPanelId overlayId = context.addPanel("overlay", "Overlay", std::make_shared<UICanvasPanel>("O"));
    const DockPanelId nativeId  = context.addPanel("native", "Native", std::make_shared<UICanvasPanel>("N"));
    const FDockFloatingWindowId overlayFloating =
        context.tearOffPanel(overlayId, {0.0f, 0.0f}, {120.0f, 80.0f});
    const FDockFloatingWindowId nativeFloating =
        context.tearOffPanel(nativeId, {40.0f, 40.0f}, {120.0f, 80.0f}, EDockFloatingProjection::NativeWindow);
    ASSERT_NE(overlayFloating, kInvalidFloatingWindowId);
    ASSERT_NE(nativeFloating, kInvalidFloatingWindowId);
    EXPECT_FALSE(context.setFloatingGeometrySpace(overlayFloating, EDockGeometrySpace::Screen));
    EXPECT_EQ(context.findFloatingById(overlayFloating)->geometrySpace, EDockGeometrySpace::TreeLocal);
    EXPECT_TRUE(context.setFloatingGeometrySpace(nativeFloating, EDockGeometrySpace::Screen));
    EXPECT_EQ(context.findFloatingById(nativeFloating)->geometrySpace, EDockGeometrySpace::Screen);
    const nlohmann::json layout = context.exportLayoutJson();
    ASSERT_EQ(layout["windows"].size(), 1u);
    EXPECT_EQ(layout["windows"][0]["geometrySpace"], "screen");
    EXPECT_EQ(layout["floating"][0]["geometrySpace"], "treeLocal");
    ASSERT_TRUE(context.setFloatingProjection(nativeFloating, EDockFloatingProjection::InProcessOverlay));
    EXPECT_EQ(context.findFloatingById(nativeFloating)->geometrySpace, EDockGeometrySpace::TreeLocal);
}

TEST(DockNodeTest, CollectLayoutPanelKeysWalksNativeWindows)
{
    const nlohmann::json layout = {
        {"version", 2},
        {"tree", {{"kind", "leaf"}, {"id", "main"}}},
        {"dockSpace", {{"main", {{"panels", nlohmann::json::array({"viewport"})}, {"selected", "viewport"}}}}},
        {"floating", nlohmann::json::array({nlohmann::json{{"panels", nlohmann::json::array({"inspector"})}}})},
        {"windows", nlohmann::json::array({nlohmann::json{{"panels", nlohmann::json::array({"hierarchy"})}}})},
    };
    const std::vector<std::string> keys = FDockContext::collectLayoutPanelKeys(layout);
    EXPECT_EQ(keys.size(), 3u);
    EXPECT_NE(std::find(keys.begin(), keys.end(), "viewport"), keys.end());
    EXPECT_NE(std::find(keys.begin(), keys.end(), "inspector"), keys.end());
    EXPECT_NE(std::find(keys.begin(), keys.end(), "hierarchy"), keys.end());
}

TEST(DockNodeTest, SanitizeLayoutJsonDropsUnknownNativeWindowKeys)
{
    const nlohmann::json layout = {
        {"version", 2},
        {"tree", {{"kind", "leaf"}, {"id", "main"}}},
        {"dockSpace", {{"main", {{"panels", nlohmann::json::array({"viewport"})}, {"selected", "viewport"}}}}},
        {"windows",
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
        layout, std::unordered_set<std::string>{"viewport", "hierarchy"});
    ASSERT_EQ(sanitized["windows"].size(), 1u);
    EXPECT_EQ(sanitized["windows"][0]["panels"], nlohmann::json::array({"hierarchy"}));
    EXPECT_EQ(sanitized["windows"][0]["selected"], "hierarchy");
}

TEST(DockNodeTest, TransferPanelMovesWidgetWithoutDualMount)
{
    WidgetTree treeA({.width = 400, .height = 300});
    WidgetTree treeB({.width = 400, .height = 300});
    auto contextA = std::make_shared<FDockContext>();
    auto contextB = std::make_shared<FDockContext>();
    auto widget = std::make_shared<UICanvasPanel>("Moved");
    const DockPanelId idA = contextA->addPanel("moved", "Moved", widget);
    ASSERT_NE(idA, kInvalidDockPanelId);
    ASSERT_NE(contextB->addPanel("keep", "Keep", std::make_shared<UICanvasPanel>("Keep")), kInvalidDockPanelId);

    auto dockA = std::make_shared<UIDockSpace>("DockA");
    auto dockB = std::make_shared<UIDockSpace>("DockB");
    dockA->setContext(contextA);
    dockB->setContext(contextB);
    FCanvasSlotArgs fill;
    fill.anchorMin = {0.0f, 0.0f};
    fill.anchorMax = {1.0f, 1.0f};
    ASSERT_TRUE(treeA.attach(*treeA.getLayer(WidgetTree::ELayer::Content), dockA, fill).valid());
    ASSERT_TRUE(treeB.attach(*treeB.getLayer(WidgetTree::ELayer::Content), dockB, fill).valid());
    (void)treeA.buildSnapshot(UIFrameBuildContext{});
    (void)treeB.buildSnapshot(UIFrameBuildContext{});
    EXPECT_TRUE(treeA.contains(*widget));
    EXPECT_FALSE(treeB.contains(*widget));

    const DockPanelId idB = contextA->transferPanelTo(*contextB, idA);
    ASSERT_NE(idB, kInvalidDockPanelId);
    EXPECT_EQ(contextA->findPanel(idA), nullptr);
    ASSERT_NE(contextB->findPanel(idB), nullptr);
    EXPECT_EQ(contextB->findPanel(idB)->widget, widget);
    EXPECT_EQ(contextB->findPanel(idB)->documentKey, "");
    EXPECT_FALSE(treeA.contains(*widget));
    EXPECT_TRUE(treeB.contains(*widget));
    EXPECT_EQ(widget->getTree(), &treeB);
}

TEST(DockNodeTest, TransferRejectsDuplicateStableKeyAndPreservesSource)
{
    FDockContext source;
    FDockContext target;
    auto widget = std::make_shared<UICanvasPanel>("Dup");
    const DockPanelId id = source.addPanel("same", "Same", widget);
    ASSERT_NE(id, kInvalidDockPanelId);
    ASSERT_NE(target.addPanel("same", "Other", std::make_shared<UICanvasPanel>("O")), kInvalidDockPanelId);
    EXPECT_EQ(source.transferPanelTo(target, id), kInvalidDockPanelId);
    EXPECT_NE(source.findPanel(id), nullptr);
    EXPECT_EQ(widget->getTree(), nullptr);
}

TEST(DockNodeTest, TransferNativePlacementMovesTornPanel)
{
    WidgetTree treeA({.width = 400, .height = 300});
    WidgetTree treeB({.width = 400, .height = 300});
    auto contextA = std::make_shared<FDockContext>();
    auto contextB = std::make_shared<FDockContext>();
    contextA->bAllowFloating = true;
    contextA->bAllowTearOff  = true;
    auto widget = std::make_shared<UICanvasPanel>("Torn");
    const DockPanelId panelId = contextA->addPanel("torn", "Torn", widget);
    ASSERT_NE(contextB->addPanel("keep", "Keep", std::make_shared<UICanvasPanel>("Keep")), kInvalidDockPanelId);

    auto dockA = std::make_shared<UIDockSpace>("DockA");
    auto dockB = std::make_shared<UIDockSpace>("DockB");
    dockA->setContext(contextA);
    dockB->setContext(contextB);
    FCanvasSlotArgs fill;
    fill.anchorMin = {0.0f, 0.0f};
    fill.anchorMax = {1.0f, 1.0f};
    ASSERT_TRUE(treeA.attach(*treeA.getLayer(WidgetTree::ELayer::Content), dockA, fill).valid());
    ASSERT_TRUE(treeB.attach(*treeB.getLayer(WidgetTree::ELayer::Content), dockB, fill).valid());
    (void)treeA.buildSnapshot(UIFrameBuildContext{});
    (void)treeB.buildSnapshot(UIFrameBuildContext{});

    const FDockFloatingWindowId placementId =
        contextA->tearOffPanel(panelId, {8.0f, 8.0f}, {180.0f, 120.0f}, EDockFloatingProjection::NativeWindow);
    ASSERT_NE(placementId, kInvalidFloatingWindowId);
    contextA->fireDockUpdated();
    EXPECT_FALSE(treeA.contains(*widget));

    ASSERT_TRUE(contextA->transferNativePlacementTo(*contextB, placementId));
    EXPECT_EQ(contextA->findPanel(panelId), nullptr);
    EXPECT_TRUE(contextA->floatingPlacements().empty());
    EXPECT_TRUE(treeB.contains(*widget));
    EXPECT_FALSE(treeA.contains(*widget));
    EXPECT_EQ(widget->getTree(), &treeB);
}

TEST(DockNodeTest, ForeignDockDropTransfersPanelWithoutDualMount)
{
    WidgetTree treeA({.width = 400, .height = 300});
    WidgetTree treeB({.width = 400, .height = 300});
    auto contextA = std::make_shared<FDockContext>();
    auto contextB = std::make_shared<FDockContext>();
    auto widget = std::make_shared<UICanvasPanel>("Moved");
    const DockPanelId idA = contextA->addPanel("moved", "Moved", widget);
    ASSERT_NE(idA, kInvalidDockPanelId);
    ASSERT_NE(contextB->addPanel("keep", "Keep", std::make_shared<UICanvasPanel>("Keep")), kInvalidDockPanelId);

    auto dockA = std::make_shared<UIDockSpace>("DockA");
    auto dockB = std::make_shared<UIDockSpace>("DockB");
    dockA->setContext(contextA);
    dockB->setContext(contextB);
    FCanvasSlotArgs fill;
    fill.anchorMin = {0.0f, 0.0f};
    fill.anchorMax = {1.0f, 1.0f};
    ASSERT_TRUE(treeA.attach(*treeA.getLayer(WidgetTree::ELayer::Content), dockA, fill).valid());
    ASSERT_TRUE(treeB.attach(*treeB.getLayer(WidgetTree::ELayer::Content), dockB, fill).valid());
    (void)treeA.buildSnapshot(UIFrameBuildContext{});
    (void)treeB.buildSnapshot(UIFrameBuildContext{});
    ASSERT_TRUE(treeA.contains(*widget));

    const UIDragDropOperationRef operation =
        FDockPanelDragDropOp::make(idA, "Moved", contextA.get());
    EXPECT_TRUE(treeB.dropExternal(*operation, {200.0f, 150.0f}));
    EXPECT_EQ(contextA->findPanel(idA), nullptr);
    EXPECT_FALSE(treeA.contains(*widget));
    EXPECT_TRUE(treeB.contains(*widget));
    EXPECT_EQ(widget->getTree(), &treeB);
}

TEST(DockNodeTest, ForeignDockChooserHoverShowsPreviewWithoutDropping)
{
    WidgetTree treeA({.width = 400, .height = 300});
    WidgetTree treeB({.width = 400, .height = 300});
    auto contextA = std::make_shared<FDockContext>();
    auto contextB = std::make_shared<FDockContext>();
    auto widget = std::make_shared<UICanvasPanel>("Moved");
    const DockPanelId idA = contextA->addPanel("moved", "Moved", widget);
    ASSERT_NE(idA, kInvalidDockPanelId);
    ASSERT_NE(contextB->addPanel("keep", "Keep", std::make_shared<UICanvasPanel>("Keep")), kInvalidDockPanelId);

    auto dockA = std::make_shared<UIDockSpace>("DockA");
    auto dockB = std::make_shared<UIDockSpace>("DockB");
    dockA->setContext(contextA);
    dockB->setContext(contextB);
    FCanvasSlotArgs fill;
    fill.anchorMin = {0.0f, 0.0f};
    fill.anchorMax = {1.0f, 1.0f};
    ASSERT_TRUE(treeA.attach(*treeA.getLayer(WidgetTree::ELayer::Content), dockA, fill).valid());
    ASSERT_TRUE(treeB.attach(*treeB.getLayer(WidgetTree::ELayer::Content), dockB, fill).valid());
    (void)treeA.buildSnapshot(UIFrameBuildContext{});
    (void)treeB.buildSnapshot(UIFrameBuildContext{});

    const UIDragDropOperationRef operation =
        FDockPanelDragDropOp::make(idA, "Moved", contextA.get());
    glm::vec2 chooserPoint{64.0f, 96.0f};
    bool bFoundChooser = false;
    for (float y = 40.0f; y <= 260.0f && !bFoundChooser; y += 16.0f) {
        for (float x = 24.0f; x <= 180.0f; x += 16.0f) {
            const auto preview = dockB->dropPreviewFor(*operation, {x, y});
            if (preview && preview->target.isPreviewOnly() && !preview->bDisabled) {
                chooserPoint = {x, y};
                bFoundChooser = true;
                break;
            }
        }
    }
    ASSERT_TRUE(bFoundChooser);
    treeB.setExternalDropHover(*operation, chooserPoint);
    EXPECT_FALSE(treeB.isDragging());
    ASSERT_NE(dynamic_cast<UIDockTabStack*>(treeB.getDropTarget()), nullptr);
    EXPECT_NE(treeB.getDropTarget(), dockB.get());
    UIElement* hoverLayer = treeB.getLayer(WidgetTree::ELayer::DragIme);
    ASSERT_NE(hoverLayer, nullptr);
    EXPECT_GE(hoverLayer->getChildren().size(), 1u);
    EXPECT_FALSE(treeB.dropExternal(*operation, chooserPoint));
    EXPECT_NE(contextA->findPanel(idA), nullptr);
    EXPECT_TRUE(treeA.contains(*widget));
    EXPECT_FALSE(treeB.contains(*widget));
}

TEST(DockNodeTest, CanAdoptPanelRejectsTransferWithoutExtracting)
{
    FDockContext source;
    FDockContext target;
    auto widget = std::make_shared<UICanvasPanel>("Owned");
    const DockPanelId id = source.addPanel("hierarchy", "Hierarchy", widget);
    ASSERT_NE(id, kInvalidDockPanelId);
    target.canAdoptPanel = [](std::string_view stableKey, uint32_t, std::string_view) {
        return stableKey != "hierarchy";
    };
    EXPECT_EQ(source.transferPanelTo(target, id), kInvalidDockPanelId);
    ASSERT_NE(source.findPanel(id), nullptr);
    EXPECT_EQ(source.findPanel(id)->widget, widget);
    EXPECT_EQ(target.findPanelByStableKey("hierarchy"), nullptr);
}

TEST(DockNodeTest, DropTargetKindDistinguishesWellStackSplitAndNoTarget)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto context = std::make_shared<FDockContext>();
    auto dock = std::make_shared<UIDockSpace>("Dock");
    dock->setContext(context);
    FCanvasSlotArgs fill;
    fill.anchorMin = {0.0f, 0.0f};
    fill.anchorMax = {1.0f, 1.0f};
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), dock, fill).valid());

    const DockPanelId sceneId = context->addPanel("scene", "Scene", std::make_shared<UICanvasPanel>("Scene"));
    const DockPanelId inspectorId =
        context->addPanel("inspector", "Inspector", std::make_shared<UICanvasPanel>("Inspector"));
    ASSERT_NE(sceneId, kInvalidDockPanelId);
    ASSERT_NE(inspectorId, kInvalidDockPanelId);
    ASSERT_TRUE(context->dockModel().splitLeaf(context->dockModel().getRootNode()->id,
                                               EDockCardinalSide::East,
                                               inspectorId));
    (void)tree.buildSnapshot(UIFrameBuildContext{});

    const FDockNode* sceneLeaf = context->dockModel().findLeafForPanel(sceneId);
    const FDockNode* inspectorLeaf = context->dockModel().findLeafForPanel(inspectorId);
    ASSERT_NE(sceneLeaf, nullptr);
    ASSERT_NE(inspectorLeaf, nullptr);
    ASSERT_NE(sceneLeaf->id, inspectorLeaf->id);

    auto* sceneBar = dynamic_cast<UIDockTabWell*>(
        findNamedDescendant(*dock, std::format("DockTabBar{}", sceneLeaf->id)));
    auto* inspectorBar = dynamic_cast<UIDockTabWell*>(
        findNamedDescendant(*dock, std::format("DockTabBar{}", inspectorLeaf->id)));
    auto* inspectorLeafRoot = dynamic_cast<UIDockTabStack*>(
        findNamedDescendant(*dock, std::format("DockStack{}", inspectorLeaf->id)));
    ASSERT_NE(sceneBar, nullptr);
    ASSERT_NE(inspectorBar, nullptr);
    ASSERT_NE(inspectorLeafRoot, nullptr);
    EXPECT_EQ(inspectorBar->area(), dock.get());
    EXPECT_EQ(inspectorLeafRoot->area(), dock.get());

    const UIDragDropOperationRef operation =
        FDockPanelDragDropOp::make(sceneId, "Scene", context.get());

    const glm::vec2 wellPoint = inspectorBar->_layoutRect.pos + inspectorBar->_layoutRect.extent * 0.5f;
    const auto well = dock->dropPreviewFor(*operation, wellPoint);
    ASSERT_TRUE(well.has_value());
    EXPECT_EQ(well->target.kind, EDockDropTargetKind::TabWell);
    EXPECT_EQ(well->target.stackId, inspectorLeaf->id);
    EXPECT_TRUE(well->target.commitsDrop());
    EXPECT_TRUE(well->target.isMerge());
    EXPECT_TRUE(inspectorBar->canAcceptDrop(*operation, wellPoint));

    const glm::vec2 sameLeafPoint = sceneBar->_layoutRect.pos + glm::vec2{8.0f, sceneBar->_layoutRect.extent.y + 40.0f};
    const auto sameLeaf = dock->dropPreviewFor(*operation, sameLeafPoint);
    ASSERT_TRUE(sameLeaf.has_value());
    EXPECT_TRUE(sameLeaf->target.kind == EDockDropTargetKind::TabStackChooser ||
                sameLeaf->target.kind == EDockDropTargetKind::TabStackSplit ||
                sameLeaf->target.kind == EDockDropTargetKind::TabStackCenter);
    auto* sceneStack = dynamic_cast<UIDockTabStack*>(
        findNamedDescendant(*dock, std::format("DockStack{}", sceneLeaf->id)));
    ASSERT_NE(sceneStack, nullptr);
    if (sameLeaf->target.commitsDrop()) {
        EXPECT_TRUE(sceneStack->canAcceptDrop(*operation, sameLeafPoint));
    }
    else {
        EXPECT_FALSE(sceneStack->canAcceptDrop(*operation, sameLeafPoint));
        EXPECT_TRUE(sceneStack->canPreviewDrop(*operation, sameLeafPoint));
    }

    glm::vec2 chooserPoint = inspectorLeafRoot->_layoutRect.pos + glm::vec2{24.0f, 80.0f};
    bool bFoundChooser = false;
    bool bFoundSplit = false;
    bool bFoundCenter = false;
    glm::vec2 splitPoint{};
    glm::vec2 centerPoint{};
    const Rect2D inspectorRect = inspectorLeafRoot->_layoutRect;
    for (float y = inspectorRect.pos.y + 40.0f; y < inspectorRect.pos.y + inspectorRect.extent.y - 8.0f; y += 8.0f) {
        for (float x = inspectorRect.pos.x + 8.0f; x < inspectorRect.pos.x + inspectorRect.extent.x - 8.0f; x += 8.0f) {
            const auto preview = dock->dropPreviewFor(*operation, {x, y});
            if (!preview) {
                continue;
            }
            if (!bFoundChooser && preview->target.kind == EDockDropTargetKind::TabStackChooser) {
                chooserPoint = {x, y};
                bFoundChooser = true;
            }
            if (!bFoundSplit && preview->target.kind == EDockDropTargetKind::TabStackSplit) {
                splitPoint = {x, y};
                bFoundSplit = true;
            }
            if (!bFoundCenter && preview->target.kind == EDockDropTargetKind::TabStackCenter &&
                preview->target.stackId == inspectorLeaf->id) {
                centerPoint = {x, y};
                bFoundCenter = true;
            }
        }
    }
    ASSERT_TRUE(bFoundChooser);
    ASSERT_TRUE(bFoundSplit);
    ASSERT_TRUE(bFoundCenter);

    const auto chooser = dock->dropPreviewFor(*operation, chooserPoint);
    ASSERT_TRUE(chooser.has_value());
    EXPECT_EQ(chooser->target.kind, EDockDropTargetKind::TabStackChooser);
    EXPECT_FALSE(chooser->target.commitsDrop());
    EXPECT_TRUE(chooser->target.isPreviewOnly());
    EXPECT_FALSE(inspectorLeafRoot->canAcceptDrop(*operation, chooserPoint));
    EXPECT_TRUE(inspectorLeafRoot->canPreviewDrop(*operation, chooserPoint));
    EXPECT_TRUE(inspectorLeafRoot->canAcceptDrop(*operation, splitPoint));

    const auto split = dock->dropPreviewFor(*operation, splitPoint);
    ASSERT_TRUE(split.has_value());
    EXPECT_EQ(split->target.kind, EDockDropTargetKind::TabStackSplit);
    EXPECT_TRUE(split->target.commitsDrop());
    EXPECT_FALSE(split->target.isMerge());

    const auto center = dock->dropPreviewFor(*operation, centerPoint);
    ASSERT_TRUE(center.has_value());
    EXPECT_EQ(center->target.kind, EDockDropTargetKind::TabStackCenter);
    EXPECT_EQ(center->target.stackId, inspectorLeaf->id);
    EXPECT_TRUE(center->target.isMerge());

    const auto outside = dock->dropPreviewFor(*operation, {-20.0f, 300.0f});
    ASSERT_TRUE(outside.has_value());
    EXPECT_EQ(outside->target.kind, EDockDropTargetKind::NoTarget);
    EXPECT_FALSE(outside->target.commitsDrop());
}

TEST(DockNodeTest, LayoutSplitsTreeNodesFromDockSpaceRecords)
{
    FDockTreeModel model;
    registerPanel(model, 1, "viewport");
    registerPanel(model, 2, "inspector");
    ASSERT_TRUE(model.addPanel(1));
    ASSERT_TRUE(model.splitLeaf(model.getRootNode()->id, EDockCardinalSide::East, 2, 0.7f));

    const nlohmann::json exported = model.exportLayoutJson();
    EXPECT_EQ(exported["version"], 2);
    EXPECT_EQ(exported["tree"]["kind"], "split");
    EXPECT_EQ(exported["tree"]["children"][0]["kind"], "leaf");
    EXPECT_FALSE(exported["tree"]["children"][0].contains("panels"));
    const std::string leftId  = exported["tree"]["children"][0]["id"].get<std::string>();
    const std::string rightId = exported["tree"]["children"][1]["id"].get<std::string>();
    EXPECT_EQ(exported["dockSpace"][leftId]["panels"], nlohmann::json::array({"viewport"}));
    EXPECT_EQ(exported["dockSpace"][rightId]["panels"], nlohmann::json::array({"inspector"}));

    // Hand-written documents use the same split: named leaves in the tree,
    // stack data keyed by the same names in dockSpace.
    const nlohmann::json handWritten = {
        {"version", 2},
        {"tree",
         {{"kind", "split"},
          {"orientation", "horizontal"},
          {"ratio", 0.7f},
          {"children",
           nlohmann::json::array({
               nlohmann::json{{"kind", "leaf"}, {"id", "page"}},
               nlohmann::json{{"kind", "leaf"}, {"id", "tools"}},
           })}}},
        {"dockSpace",
         {
             {"page", {{"role", "page"}, {"panels", nlohmann::json::array({"viewport"})}}},
             {"tools", {{"role", "tools"}, {"panels", nlohmann::json::array({"inspector"})}}},
         }},
    };
    FDockTreeModel restored;
    registerPanel(restored, 1, "viewport");
    registerPanel(restored, 2, "inspector");
    ASSERT_TRUE(restored.addPanel(1));
    ASSERT_TRUE(restored.addPanel(2));
    ASSERT_TRUE(restored.importLayoutJson(handWritten));
    ASSERT_NE(restored.findLeafForPanel(1), nullptr);
    ASSERT_NE(restored.findLeafForPanel(2), nullptr);
    EXPECT_EQ(restored.findFirstLeafWithRole(EDockLeafRole::Page), restored.findLeafForPanel(1)->id);
    EXPECT_EQ(restored.findFirstLeafWithRole(EDockLeafRole::Tools), restored.findLeafForPanel(2)->id);
    EXPECT_EQ(restored.getRootNode()->orientation, EDockSplitOrientation::Horizontal);
    EXPECT_FLOAT_EQ(restored.getRootNode()->ratio, 0.7f);
    EXPECT_TRUE(restored.validateInvariants());
}

TEST(DockNodeTest, CommitDropMovesPanelBetweenStacks)
{
    FDockContext context;
    const DockPanelId sceneId = context.addPanel("scene", "Scene", std::make_shared<UICanvasPanel>("Scene"));
    const DockPanelId inspectorId =
        context.addPanel("inspector", "Inspector", std::make_shared<UICanvasPanel>("Inspector"));
    ASSERT_NE(sceneId, kInvalidDockPanelId);
    ASSERT_NE(inspectorId, kInvalidDockPanelId);
    ASSERT_TRUE(context.layout().splitStack(context.layout().getRootNode()->id,
                                            EDockCardinalSide::East,
                                            inspectorId));
    const FDockNode* inspectorStack = context.layout().findStackForPanel(inspectorId);
    const FDockNode* sceneStack = context.layout().findStackForPanel(sceneId);
    ASSERT_NE(inspectorStack, nullptr);
    ASSERT_NE(sceneStack, nullptr);
    ASSERT_NE(inspectorStack->id, sceneStack->id);

    EXPECT_EQ(context.commitDrop(sceneId, FDockDropTarget::well(inspectorStack->id, 0)),
              EDockDropCommit::Applied);
    EXPECT_EQ(context.layout().findStackForPanel(sceneId)->id, inspectorStack->id);
    EXPECT_EQ(context.layout().findStackForPanel(inspectorId)->id, inspectorStack->id);
    EXPECT_EQ(context.commitDrop(inspectorId, FDockDropTarget::stackCenter(inspectorStack->id)),
              EDockDropCommit::Selected);
}

TEST(DockNodeTest, CommitDropSameStackChooserSplitApplies)
{
    FDockContext context;
    const DockPanelId sceneId = context.addPanel("scene", "Scene", std::make_shared<UICanvasPanel>("Scene"));
    const DockPanelId inspectorId =
        context.addPanel("inspector", "Inspector", std::make_shared<UICanvasPanel>("Inspector"));
    ASSERT_NE(sceneId, kInvalidDockPanelId);
    ASSERT_NE(inspectorId, kInvalidDockPanelId);
    const FDockNode* root = context.layout().getRootNode();
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->kind, EDockNodeKind::Stack);
    EXPECT_EQ(root->panelIds.size(), 2u);

    EXPECT_EQ(context.commitDrop(inspectorId, FDockDropTarget::stackSplit(root->id, EDockCardinalSide::East)),
              EDockDropCommit::Applied);
    const FDockNode* sceneStack = context.layout().findStackForPanel(sceneId);
    const FDockNode* inspectorStack = context.layout().findStackForPanel(inspectorId);
    ASSERT_NE(sceneStack, nullptr);
    ASSERT_NE(inspectorStack, nullptr);
    EXPECT_NE(sceneStack->id, inspectorStack->id);
    EXPECT_EQ(context.layout().getRootNode()->kind, EDockNodeKind::Split);
}

TEST(DockNodeTest, SameStackContentShowsChooserNotSilentCenter)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto context = std::make_shared<FDockContext>();
    auto dock = std::make_shared<UIDockSpace>("Dock");
    dock->setContext(context);
    FCanvasSlotArgs fill;
    fill.anchorMin = {0.0f, 0.0f};
    fill.anchorMax = {1.0f, 1.0f};
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), dock, fill).valid());

    const DockPanelId sceneId = context->addPanel("scene", "Scene", std::make_shared<UICanvasPanel>("Scene"));
    const DockPanelId inspectorId =
        context->addPanel("inspector", "Inspector", std::make_shared<UICanvasPanel>("Inspector"));
    ASSERT_NE(sceneId, kInvalidDockPanelId);
    ASSERT_NE(inspectorId, kInvalidDockPanelId);
    (void)tree.buildSnapshot(UIFrameBuildContext{});

    const FDockNode* stack = context->dockModel().findStackForPanel(sceneId);
    ASSERT_NE(stack, nullptr);
    EXPECT_EQ(stack->panelIds.size(), 2u);
    auto* stackRoot = dynamic_cast<UIDockTabStack*>(
        findNamedDescendant(*dock, std::format("DockStack{}", stack->id)));
    ASSERT_NE(stackRoot, nullptr);

    const UIDragDropOperationRef operation =
        FDockPanelDragDropOp::make(inspectorId, "Inspector", context.get());
    bool bFoundChooser = false;
    bool bFoundSplit = false;
    const Rect2D rect = stackRoot->_layoutRect;
    for (float y = rect.pos.y + 40.0f; y < rect.pos.y + rect.extent.y - 8.0f; y += 8.0f) {
        for (float x = rect.pos.x + 8.0f; x < rect.pos.x + rect.extent.x - 8.0f; x += 8.0f) {
            const auto preview = dock->dropPreviewFor(*operation, {x, y});
            if (!preview) {
                continue;
            }
            if (preview->target.kind == EDockDropTargetKind::TabStackChooser) {
                bFoundChooser = true;
            }
            if (preview->target.kind == EDockDropTargetKind::TabStackSplit) {
                bFoundSplit = true;
            }
        }
    }
    EXPECT_TRUE(bFoundChooser);
    EXPECT_TRUE(bFoundSplit);
}

TEST(DockNodeTest, FloatingWindowProducesDropTargetWithoutDockSpace)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto context = std::make_shared<FDockContext>();
    context->bAllowFloating = true;
    context->bAllowTearOff = true;

    auto dock = std::make_shared<UIDockSpace>("Dock");
    dock->setContext(context);
    FCanvasSlotArgs fill;
    fill.anchorMin = {0.0f, 0.0f};
    fill.anchorMax = {1.0f, 1.0f};
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), dock, fill).valid());

    auto host = std::make_shared<UIDockFloatingHost>("Host");
    host->bindContext(context);
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Popup), host, fill).valid());

    const DockPanelId sceneId = context->addPanel("scene", "Scene", std::make_shared<UICanvasPanel>("Scene"));
    const DockPanelId inspectorId =
        context->addPanel("inspector", "Inspector", std::make_shared<UICanvasPanel>("Inspector"));
    ASSERT_NE(sceneId, kInvalidDockPanelId);
    ASSERT_NE(inspectorId, kInvalidDockPanelId);
    const FDockFloatingWindowId floatingId =
        context->tearOffPanel(inspectorId, {40.0f, 40.0f}, {240.0f, 180.0f});
    ASSERT_NE(floatingId, kInvalidFloatingWindowId);
    host->syncFromContext();
    (void)tree.buildSnapshot(UIFrameBuildContext{});

    auto* floating = dynamic_cast<UIDockFloatingWindow*>(
        findNamedDescendant(*host, std::format("FloatingWindow{}", floatingId)));
    ASSERT_NE(floating, nullptr);

    const glm::vec2 overWindow = floating->_layoutRect.pos + floating->_layoutRect.extent * 0.5f;
    const auto merge = floating->dropTargetAt(overWindow, sceneId);
    ASSERT_TRUE(merge.has_value());
    EXPECT_EQ(merge->kind, EDockDropTargetKind::FloatingTabWell);
    EXPECT_EQ(merge->floatingWindowId, floatingId);
    EXPECT_TRUE(merge->commitsDrop());

    EXPECT_FALSE(floating->dropTargetAt(overWindow, inspectorId).has_value());
    EXPECT_FALSE(floating->dropTargetAt({-20.0f, 300.0f}, sceneId).has_value());

    EXPECT_EQ(context->commitDrop(sceneId, *merge), EDockDropCommit::Applied);
    EXPECT_TRUE(context->isPanelFloating(sceneId));
    EXPECT_EQ(context->findFloatingByPanel(sceneId)->id, floatingId);
}

TEST(DockNodeTest, NestedDockChooserWinsOverHostPageLeaf)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto outer = std::make_shared<FDockContext>();
    auto nested = std::make_shared<FDockContext>();
    auto nestedDock = std::make_shared<UIDockSpace>("NestedDock");
    nestedDock->setContext(nested);
    ASSERT_NE(nested->addPanel("viewport", "Viewport", std::make_shared<UICanvasPanel>("ViewportBody")),
              kInvalidDockPanelId);

    auto outerDock = std::make_shared<UIDockSpace>("OuterDock");
    outerDock->setContext(outer);
    const DockPanelId pageId =
        outer->addPanel("page", "Page", nestedDock);
    const DockPanelId contentId =
        outer->addPanel("content", "Content", std::make_shared<UICanvasPanel>("ContentBody"));
    ASSERT_NE(pageId, kInvalidDockPanelId);
    ASSERT_NE(contentId, kInvalidDockPanelId);
    ASSERT_TRUE(outer->layout().splitStack(outer->layout().getRootNode()->id,
                                           EDockCardinalSide::South,
                                           contentId,
                                           0.50f));
    outer->fireDockUpdated();

    FCanvasSlotArgs fill;
    fill.anchorMin = {0.0f, 0.0f};
    fill.anchorMax = {1.0f, 1.0f};
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), outerDock, fill).valid());
    (void)tree.buildSnapshot(UIFrameBuildContext{});

    auto* nestedStack = dynamic_cast<UIDockTabStack*>(
        findNamedDescendant(*nestedDock, std::format("DockStack{}", nested->layout().getRootNode()->id)));
    auto* pageStack = dynamic_cast<UIDockTabStack*>(
        findNamedDescendant(*outerDock, std::format("DockStack{}", outer->layout().findStackForPanel(pageId)->id)));
    ASSERT_NE(nestedStack, nullptr);
    ASSERT_NE(pageStack, nullptr);
    EXPECT_GT(nestedStack->_layoutRect.extent.x, 80.0f);
    EXPECT_GT(nestedStack->_layoutRect.extent.y, 80.0f);

    const UIDragDropOperationRef operation =
        FDockPanelDragDropOp::make(contentId, "Content", outer.get());
    glm::vec2 chooserPoint{};
    glm::vec2 splitPoint{};
    bool bFoundChooser = false;
    bool bFoundSplit = false;
    const Rect2D nestedRect = nestedStack->_layoutRect;
    for (float y = nestedRect.pos.y + 8.0f; y < nestedRect.pos.y + nestedRect.extent.y - 8.0f; y += 8.0f) {
        for (float x = nestedRect.pos.x + 8.0f; x < nestedRect.pos.x + nestedRect.extent.x - 8.0f; x += 8.0f) {
            const auto nestedPreview = nestedDock->dropPreviewFor(*operation, {x, y});
            if (!nestedPreview || nestedPreview->bDisabled) {
                continue;
            }
            if (!bFoundChooser && nestedPreview->target.kind == EDockDropTargetKind::TabStackChooser) {
                chooserPoint = {x, y};
                bFoundChooser = true;
            }
            if (!bFoundSplit && nestedPreview->target.kind == EDockDropTargetKind::TabStackSplit) {
                splitPoint = {x, y};
                bFoundSplit = true;
            }
        }
    }
    ASSERT_TRUE(bFoundChooser);
    ASSERT_TRUE(bFoundSplit);

    EXPECT_FALSE(outerDock->dropPreviewFor(*operation, chooserPoint).has_value());
    EXPECT_FALSE(pageStack->canAcceptDrop(*operation, chooserPoint));
    EXPECT_FALSE(pageStack->canPreviewDrop(*operation, chooserPoint));
    EXPECT_TRUE(nestedStack->canPreviewDrop(*operation, chooserPoint));
    EXPECT_FALSE(nestedStack->canAcceptDrop(*operation, chooserPoint));
    EXPECT_TRUE(nestedStack->canAcceptDrop(*operation, splitPoint));
    EXPECT_FALSE(pageStack->canAcceptDrop(*operation, splitPoint));

    tree.beginDrag(outerDock.get(), operation);
    tree.updateDrag(chooserPoint);
    EXPECT_EQ(tree.getDropTarget(), nestedStack);
    EXPECT_TRUE(nestedDock->hasDropPreview());
    EXPECT_TRUE(nestedDock->isDropPreviewChooser());
    EXPECT_FALSE(outerDock->hasDropPreview());

    tree.endDrag(splitPoint);
    EXPECT_FALSE(tree.isDragging());
    EXPECT_EQ(outer->findPanel(contentId), nullptr);
    const FDockContext::FPanel* imported = nested->findPanelByStableKey("content");
    ASSERT_NE(imported, nullptr);
    const FDockContext::FPanel* viewportPanel = nested->findPanelByStableKey("viewport");
    ASSERT_NE(viewportPanel, nullptr);
    const FDockNode* nestedRoot = nested->layout().getRootNode();
    ASSERT_NE(nestedRoot, nullptr);
    EXPECT_EQ(nestedRoot->kind, EDockNodeKind::Split);
    EXPECT_NE(nested->layout().findStackForPanel(imported->id),
              nested->layout().findStackForPanel(viewportPanel->id));
    EXPECT_EQ(outer->layout().leafIds().size(), 1u);
    ASSERT_NE(outer->layout().getRootNode(), nullptr);
    EXPECT_EQ(outer->layout().getRootNode()->kind, EDockNodeKind::Stack);
}

TEST(DockNodeTest, PruneEmptyGenericLeavesKeepsPageWell)
{
    FDockTreeModel model;
    registerPanel(model, 1, "level");
    ASSERT_TRUE(model.addPanel(1));
    const DockNodeId rootId = model.getRootNode()->id;
    ASSERT_TRUE(model.setLeafRole(rootId, EDockLeafRole::Page));
    ASSERT_TRUE(model.splitEmptyLeaf(rootId, EDockCardinalSide::East, 0.5f, true));
    EXPECT_EQ(model.leafIds().size(), 2u);
    model.pruneEmptyGenericLeaves();
    EXPECT_EQ(model.leafIds().size(), 1u);
    EXPECT_EQ(model.findFirstLeafWithRole(EDockLeafRole::Page), model.getRootNode()->id);

    ASSERT_TRUE(model.splitEmptyLeaf(model.getRootNode()->id, EDockCardinalSide::South, 0.78f, true));
    FDockNode* split = model.findNode(model.getRootNode()->id);
    ASSERT_NE(split, nullptr);
    ASSERT_EQ(split->kind, EDockNodeKind::Split);
    ASSERT_TRUE(model.setLeafRole(split->child[1]->id, EDockLeafRole::Tools));
    EXPECT_EQ(model.leafIds().size(), 2u);
    model.pruneEmptyGenericLeaves();
    EXPECT_EQ(model.leafIds().size(), 1u);
    EXPECT_NE(model.findFirstLeafWithRole(EDockLeafRole::Page), kInvalidDockNodeId);
    EXPECT_EQ(model.findFirstLeafWithRole(EDockLeafRole::Tools), kInvalidDockNodeId);
}

TEST(DockNodeTest, LastPanelLeavingPersistentToolsLeafCollapses)
{
    FDockTreeModel model;
    registerPanel(model, 1, "level");
    registerPanel(model, 2, "content");
    ASSERT_TRUE(model.addPanel(1));
    const DockNodeId rootId = model.getRootNode()->id;
    ASSERT_TRUE(model.setLeafRole(rootId, EDockLeafRole::Page));
    ASSERT_TRUE(model.splitEmptyLeaf(rootId, EDockCardinalSide::South, 0.78f, true));
    FDockNode* split = model.findNode(model.getRootNode()->id);
    ASSERT_NE(split, nullptr);
    ASSERT_EQ(split->kind, EDockNodeKind::Split);
    ASSERT_TRUE(model.setLeafRole(split->child[1]->id, EDockLeafRole::Tools));
    ASSERT_TRUE(model.addPanel(2, split->child[1]->id));
    EXPECT_FALSE(model.findLeafForPanel(2)->persistentEmptyLeaf);
    EXPECT_EQ(model.leafIds().size(), 2u);

    ASSERT_TRUE(model.removePanel(2));
    EXPECT_EQ(model.leafIds().size(), 1u);
    EXPECT_NE(model.findFirstLeafWithRole(EDockLeafRole::Page), kInvalidDockNodeId);
    EXPECT_EQ(model.findFirstLeafWithRole(EDockLeafRole::Tools), kInvalidDockNodeId);
}

} // namespace ya
