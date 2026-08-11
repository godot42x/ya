// ToolWorkspace regression tests (gui-app-bootstrap Phase 3): document
// commands, selection transitions and inspector mutations are pure state
// transitions, independent of UIElement/WidgetTree.

#include "GUIWorkbenchWorkspace.h"

#include <gtest/gtest.h>

namespace guiworkbench
{

TEST(WorkspaceTest, NewDocumentCreatesDefaultItemAndSelectsIt)
{
    FWorkbenchWorkspace ws;
    ws.newDocument();

    ASSERT_EQ(ws.items.size(), 1u);
    EXPECT_EQ(ws.items[0].name, "Untitled");
    EXPECT_EQ(ws.selectedId, "item.untitled");
    EXPECT_FALSE(ws.bDirty);
    EXPECT_EQ(ws.commandResult, "New: created 'Untitled'");
    ASSERT_NE(ws.getSelected(), nullptr);
    EXPECT_EQ(ws.getSelectedIndex(), 0);
}

TEST(WorkspaceTest, OpenDocumentLoadsMockAndSelectsFirst)
{
    FWorkbenchWorkspace ws;
    ws.openDocument();

    ASSERT_EQ(ws.items.size(), 3u);
    EXPECT_EQ(ws.items[0].name, "Cube");
    EXPECT_EQ(ws.items[1].name, "Sphere");
    EXPECT_EQ(ws.items[2].name, "Light");
    EXPECT_EQ(ws.selectedId, "item.cube");
    EXPECT_FALSE(ws.bDirty);
    EXPECT_EQ(ws.commandResult, "Open: loaded mock document");
}

TEST(WorkspaceTest, SaveClearsDirtyOnlyWhenChanged)
{
    FWorkbenchWorkspace ws;
    ws.openDocument();

    ws.saveDocument();
    EXPECT_EQ(ws.commandResult, "Save: nothing to save");

    ws.renameSelected("Renamed");
    EXPECT_TRUE(ws.bDirty);
    ws.saveDocument();
    EXPECT_FALSE(ws.bDirty);
    EXPECT_EQ(ws.commandResult, "Save: saved changes");
}

TEST(WorkspaceTest, ReloadRestoresMockDocument)
{
    FWorkbenchWorkspace ws;
    ws.openDocument();
    ws.select("item.light");
    ws.toggleSelectedVisible();
    ws.renameSelected("Touched");
    EXPECT_TRUE(ws.bDirty);

    ws.reloadDocument();
    EXPECT_FALSE(ws.bDirty);
    EXPECT_EQ(ws.selectedId, "item.cube");
    EXPECT_EQ(ws.commandResult, "Reload: restored mock document");
    // The Light item is back to its authored hidden state.
    EXPECT_FALSE(ws.items[2].bVisible);
    EXPECT_EQ(ws.items[2].name, "Light");
}

TEST(WorkspaceTest, SelectIgnoresUnknownIds)
{
    FWorkbenchWorkspace ws;
    ws.openDocument();

    ws.select("item.missing");
    EXPECT_EQ(ws.selectedId, "item.cube"); // unchanged
    ws.select("item.light");
    EXPECT_EQ(ws.selectedId, "item.light");
}

TEST(WorkspaceTest, SelectRelativeClampsAtBothEnds)
{
    FWorkbenchWorkspace ws;
    ws.openDocument();

    ws.selectRelative(1);
    EXPECT_EQ(ws.selectedId, "item.sphere");
    ws.selectRelative(1);
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
    ws.openDocument();
    ws.select("item.sphere");

    ws.renameSelected("SphereV2");
    EXPECT_TRUE(ws.bDirty);
    ASSERT_NE(ws.getSelected(), nullptr);
    EXPECT_EQ(ws.getSelected()->name, "SphereV2");

    const glm::vec4 beforeColor = ws.getSelected()->color;
    ws.cycleSelectedColor();
    EXPECT_NE(ws.getSelected()->color, beforeColor);

    const glm::vec2 beforeSize = ws.getSelected()->size;
    ws.stepSelectedSize({20.0f, 20.0f});
    EXPECT_EQ(ws.getSelected()->size, beforeSize + glm::vec2(20.0f));
    ws.stepSelectedSize({-500.0f, -500.0f});
    EXPECT_EQ(ws.getSelected()->size, glm::vec2(20.0f)); // clamped

    const bool beforeVisible = ws.getSelected()->bVisible;
    ws.toggleSelectedVisible();
    EXPECT_NE(ws.getSelected()->bVisible, beforeVisible);
}

TEST(WorkspaceTest, MutationsWithoutSelectionAreNoOps)
{
    FWorkbenchWorkspace ws; // default-constructed: no document, no selection

    ws.renameSelected("X");
    ws.toggleSelectedVisible();
    ws.cycleSelectedColor();
    ws.stepSelectedSize({10.0f, 10.0f});

    // Nothing to mutate: no dirty flag, no items.
    EXPECT_TRUE(ws.items.empty());
    EXPECT_FALSE(ws.bDirty);
}

} // namespace guiworkbench
