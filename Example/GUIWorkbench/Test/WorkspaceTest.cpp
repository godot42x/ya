// ToolWorkspace regression tests (gui-app-bootstrap Phase 3): app-state
// commands, selection transitions and inspector mutations are pure state
// transitions, independent of widgets and the live tree.

#include "GUI/Tooling/Workbench/WorkbenchWorkspace.h"

#include <gtest/gtest.h>

namespace guiworkbench
{

TEST(WorkspaceTest, ResetLayoutRestoresDefaultGraphAndSelection)
{
    FWorkbenchWorkspace ws;
    ws.resetLayout();

    ASSERT_EQ(ws.items.size(), 3u);
    EXPECT_EQ(ws.items[0].name, "Cube");
    EXPECT_EQ(ws.items[1].name, "Sphere");
    EXPECT_EQ(ws.items[2].name, "Light");
    EXPECT_EQ(ws.selectedId, "item.cube");
    EXPECT_EQ(ws.getSelectedIndex(), 0);
    EXPECT_FALSE(ws.bDirty);
    EXPECT_EQ(ws.commandResult, "Reset: layout restored");
}

TEST(WorkspaceTest, AddAppendsSelectsAndMarksDirty)
{
    FWorkbenchWorkspace ws;
    ws.resetLayout();

    ws.addItem("Item 4");
    ASSERT_EQ(ws.items.size(), 4u);
    ASSERT_NE(ws.getSelected(), nullptr);
    EXPECT_EQ(ws.getSelected()->name, "Item 4");
    EXPECT_EQ(ws.getSelectedIndex(), 3);
    EXPECT_TRUE(ws.bDirty);
    EXPECT_EQ(ws.commandResult, "Add: 'Item 4'");
}

TEST(WorkspaceTest, RemoveSelectedFallsBackToNeighbor)
{
    FWorkbenchWorkspace ws;
    ws.resetLayout();

    ws.select("item.light");
    ws.removeSelected();
    ASSERT_EQ(ws.items.size(), 2u);
    // Removal keeps a stable selection: the item that took the removed slot.
    EXPECT_EQ(ws.selectedId, "item.sphere");
    EXPECT_TRUE(ws.bDirty);

    ws.removeSelected();
    ws.removeSelected();
    EXPECT_TRUE(ws.items.empty());
    EXPECT_TRUE(ws.selectedId.empty());
    EXPECT_EQ(ws.getSelected(), nullptr);

    // Removing with no selection is a no-op with visible feedback.
    ws.removeSelected();
    EXPECT_EQ(ws.commandResult, "Remove: nothing selected");
}

TEST(WorkspaceTest, ReparentMovesItemUnderParentAndIndents)
{
    FWorkbenchWorkspace ws;
    ws.resetLayout();

    EXPECT_TRUE(ws.reparent("item.sphere", "item.cube"));
    EXPECT_EQ(ws.getDepth("item.sphere"), 1);
    EXPECT_EQ(ws.getDepth("item.cube"), 0);

    // Tree order: cube first, then its child sphere, then light.
    const auto ordered = ws.orderedItems();
    ASSERT_EQ(ordered.size(), 3u);
    EXPECT_EQ(ordered[0]->id, "item.cube");
    EXPECT_EQ(ordered[1]->id, "item.sphere");
    EXPECT_EQ(ordered[2]->id, "item.light");
}

TEST(WorkspaceTest, ReparentRejectsCyclesAndUnknownParents)
{
    FWorkbenchWorkspace ws;
    ws.resetLayout();

    EXPECT_TRUE(ws.reparent("item.sphere", "item.cube"));
    // Self parenting and descendant parenting are rejected.
    EXPECT_FALSE(ws.reparent("item.sphere", "item.sphere"));
    EXPECT_FALSE(ws.reparent("item.cube", "item.sphere"));
    EXPECT_FALSE(ws.reparent("item.cube", "item.missing"));
    // Original structure is untouched.
    EXPECT_EQ(ws.getDepth("item.sphere"), 1);
    EXPECT_EQ(ws.getDepth("item.cube"), 0);
}

TEST(WorkspaceTest, RenameMutatesSelectedAndReports)
{
    FWorkbenchWorkspace ws;
    ws.resetLayout();
    ws.select("item.sphere");

    ws.renameSelected("SphereV2");
    ASSERT_NE(ws.getSelected(), nullptr);
    EXPECT_EQ(ws.getSelected()->name, "SphereV2");
    EXPECT_TRUE(ws.bDirty);
    EXPECT_EQ(ws.commandResult, "Rename: 'SphereV2'");

    // Renaming with nothing selected reports feedback without mutation.
    FWorkbenchWorkspace empty;
    empty.renameSelected("X");
    EXPECT_TRUE(empty.items.empty());
    EXPECT_EQ(empty.commandResult, "Rename: nothing selected");
}

TEST(WorkspaceTest, SelectValidatesIdsAndRelativeNavigationClamps)
{
    FWorkbenchWorkspace ws;
    ws.resetLayout();

    ws.select("item.missing");
    EXPECT_EQ(ws.selectedId, "item.cube"); // unknown id: no-op
    ws.select("item.light");
    EXPECT_EQ(ws.selectedId, "item.light");

    ws.selectRelative(1);
    EXPECT_EQ(ws.selectedId, "item.light"); // clamped at the end
    ws.selectRelative(-2);
    EXPECT_EQ(ws.selectedId, "item.cube");
    ws.selectRelative(-1);
    EXPECT_EQ(ws.selectedId, "item.cube"); // clamped at the start
}

TEST(WorkspaceTest, InspectorMutationsMarkDirtyAndMutateSelected)
{
    FWorkbenchWorkspace ws;
    ws.resetLayout();
    ws.select("item.sphere");

    const bool beforeVisible = ws.getSelected()->bVisible;
    ws.toggleSelectedVisible();
    EXPECT_NE(ws.getSelected()->bVisible, beforeVisible);
    EXPECT_TRUE(ws.bDirty);

    const glm::vec4 beforeColor = ws.getSelected()->color;
    ws.cycleSelectedColor();
    EXPECT_NE(ws.getSelected()->color, beforeColor);

    const glm::vec2 beforeSize = ws.getSelected()->size;
    ws.stepSelectedSize({20.0f, 20.0f});
    EXPECT_EQ(ws.getSelected()->size, beforeSize + glm::vec2(20.0f));
    ws.stepSelectedSize({-500.0f, -500.0f});
    EXPECT_EQ(ws.getSelected()->size, glm::vec2(20.0f)); // clamped floor

    // No selection: mutations are no-ops with feedback.
    FWorkbenchWorkspace empty;
    empty.toggleSelectedVisible();
    empty.cycleSelectedColor();
    empty.stepSelectedSize({10.0f, 10.0f});
    EXPECT_FALSE(empty.bDirty);
    EXPECT_EQ(empty.commandResult, "Inspector: no selection");
}

} // namespace guiworkbench
