#include "GUI/Widgets/Controls/DockNode.h"

#include <gtest/gtest.h>

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
    const DockNodeId rootId = model.root()->id;

    ASSERT_TRUE(model.splitLeaf(rootId, EDockCardinalSide::East, 2));
    ASSERT_EQ(model.root()->kind, EDockNodeKind::Split);
    ASSERT_EQ(model.root()->child[0]->parent, model.root());
    ASSERT_EQ(model.root()->child[1]->parent, model.root());
    EXPECT_EQ(model.root()->orientation, EDockSplitOrientation::Vertical);
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
    ASSERT_TRUE(model.splitLeaf(model.root()->id, EDockCardinalSide::West, 2));
    auto* target = model.findLeafForPanel(1);
    auto* source = model.findLeafForPanel(2);
    ASSERT_NE(target, nullptr);
    ASSERT_NE(source, nullptr);
    ASSERT_NE(target, source);
    ASSERT_TRUE(model.addPanel(3, target->id));

    ASSERT_TRUE(model.movePanel(2, target->id, 1));
    EXPECT_EQ(model.root()->kind, EDockNodeKind::Leaf);
    EXPECT_EQ(model.root()->panelIds, (std::vector<DockPanelId>{1, 2, 3}));
    EXPECT_EQ(model.root()->selectedPanel, 2);
    EXPECT_TRUE(model.validateInvariants());
}

TEST(DockNodeTest, InvalidMutationDoesNotChangeModel)
{
    FDockTreeModel model;
    registerPanel(model, 1, "scene");
    registerPanel(model, 2, "inspector");
    ASSERT_TRUE(model.addPanel(1));
    const DockNodeId rootId = model.root()->id;

    EXPECT_FALSE(model.movePanel(2, rootId));
    EXPECT_FALSE(model.splitLeaf(rootId, EDockCardinalSide::North, 99));
    EXPECT_EQ(model.root()->kind, EDockNodeKind::Leaf);
    EXPECT_EQ(model.root()->panelIds, std::vector<DockPanelId>({1}));
    EXPECT_TRUE(model.validateInvariants());
}

TEST(DockNodeTest, MoveToSameLeafIsAtomicNoOp)
{
    FDockTreeModel model;
    registerPanel(model, 1, "scene");
    ASSERT_TRUE(model.addPanel(1));
    const auto* before = model.findLeafForPanel(1);
    ASSERT_NE(before, nullptr);
    const auto beforePanels = before->panelIds;
    EXPECT_FALSE(model.movePanel(1, before->id));
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
    ASSERT_TRUE(model.splitLeaf(model.root()->id, EDockCardinalSide::South, 2, 4.0f));
    EXPECT_FLOAT_EQ(model.root()->ratio, 1.0f);
    model.root()->minExtent[0] = -1.0f;
    std::string error;
    EXPECT_FALSE(model.validateInvariants(&error));
    EXPECT_FALSE(error.empty());
}

TEST(DockNodeTest, RemovePanelDeletesRegistryRecord)
{
    FDockTreeModel model;
    registerPanel(model, 1, "scene");
    ASSERT_TRUE(model.addPanel(1));
    ASSERT_TRUE(model.removePanel(1));
    EXPECT_EQ(model.panelCount(), 0u);
    EXPECT_EQ(model.findPanel(1), nullptr);
    EXPECT_TRUE(model.root()->panelIds.empty());
    EXPECT_TRUE(model.validateInvariants());
}

} // namespace ya
