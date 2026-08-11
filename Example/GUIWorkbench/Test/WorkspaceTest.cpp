// ToolWorkspace regression tests (gui-app-bootstrap): document commands,
// selection transitions and flatten order are pure state transitions,
// independent of the live WidgetTree.

#include "GUIWorkbenchWorkspace.h"

#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/UITypeRegistry.h"

#include <gtest/gtest.h>

namespace guiworkbench
{

TEST(WorkspaceTest, NewDocumentCreatesDefaultRootAndSelectsIt)
{
    FWorkbenchWorkspace ws;
    ws.newDocument("engine.panel");

    ASSERT_NE(ws.document, nullptr);
    EXPECT_EQ(ws.document->typeId, "engine.panel");
    EXPECT_TRUE(ws.documentPath.empty());
    EXPECT_EQ(ws.selectedPath, "");
    EXPECT_FALSE(ws.bDirty);
    EXPECT_EQ(ws.commandResult, "New: panel");

    const auto rows = ws.flattenRows();
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].path, "");
    EXPECT_EQ(rows[0].name, "Untitled");
    EXPECT_EQ(rows[0].typeId, "engine.panel");
    EXPECT_EQ(rows[0].depth, 0);

    // The template's root name must survive instantiation (the presenter's
    // preview + row labels depend on it).
    auto root = ws.document->instantiate();
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->_name, "Untitled");
}

TEST(WorkspaceTest, AddChildrenAppearsInFlattenOrder)
{
    FWorkbenchWorkspace ws;
    ws.newDocument("engine.container");

    // Simulate the presenter's palette add: instantiate, attach, rebuild.
    auto root = ws.document->instantiate();
    ASSERT_NE(root, nullptr);
    auto button = ya::UITypeRegistry::instance().createInstance("engine.button");
    ASSERT_NE(button, nullptr);
    root->addDetachedChild(button);
    ws.rebuildFromPreview(*root);

    const auto rows = ws.flattenRows();
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].path, "");
    EXPECT_EQ(rows[1].path, "0");
    EXPECT_EQ(rows[1].name, "Button");
    EXPECT_EQ(rows[1].depth, 1);
}

TEST(WorkspaceTest, SaveRequiresPathAndClearsDirty)
{
    FWorkbenchWorkspace ws;
    ws.newDocument("engine.panel");

    EXPECT_FALSE(ws.saveDocument()); // no path yet
    EXPECT_EQ(ws.commandResult, "Save: no path (use Save As)");

    ws.recordMutation();
    EXPECT_TRUE(ws.bDirty);

    EXPECT_TRUE(ws.saveDocumentAs("Engine/Saved/GUIWorkbench/test.yaui"));
    EXPECT_FALSE(ws.bDirty);
    EXPECT_EQ(ws.documentPath, "Engine/Saved/GUIWorkbench/test.yaui");

    // The written file parses back as the same document.
    FWorkbenchWorkspace reloaded;
    EXPECT_TRUE(reloaded.openDocument("Engine/Saved/GUIWorkbench/test.yaui"));
    ASSERT_NE(reloaded.document, nullptr);
    EXPECT_EQ(reloaded.document->typeId, "engine.panel");
}

TEST(WorkspaceTest, OpenMissingFileReportsDiagnostic)
{
    FWorkbenchWorkspace ws;
    EXPECT_FALSE(ws.openDocument("Engine/Saved/GUIWorkbench/does_not_exist.yaui"));
    EXPECT_EQ(ws.document, nullptr);
    EXPECT_FALSE(ws.commandResult.empty());
}

TEST(WorkspaceTest, SelectValidatesPathAndRelativeNavigationClamps)
{
    FWorkbenchWorkspace ws;
    ws.newDocument("engine.container");
    auto root = ws.document->instantiate();
    auto a    = ya::UITypeRegistry::instance().createInstance("engine.button");
    auto b    = ya::UITypeRegistry::instance().createInstance("engine.button");
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    root->addDetachedChild(a);
    root->addDetachedChild(b);
    ws.rebuildFromPreview(*root);

    ws.select("0");
    EXPECT_EQ(ws.selectedPath, "0");
    ws.select("99"); // unknown path: no-op
    EXPECT_EQ(ws.selectedPath, "0");

    ws.selectRelative(1);
    EXPECT_EQ(ws.selectedPath, "1");
    ws.selectRelative(1);
    EXPECT_EQ(ws.selectedPath, "1"); // clamped at the end
    ws.selectRelative(-2);
    EXPECT_EQ(ws.selectedPath, "");
    ws.selectRelative(-1);
    EXPECT_EQ(ws.selectedPath, ""); // clamped at the start
}

TEST(WorkspaceTest, CloseDocumentClearsState)
{
    FWorkbenchWorkspace ws;
    ws.newDocument("engine.panel");
    ws.recordMutation();
    ws.closeDocument();

    EXPECT_EQ(ws.document, nullptr);
    EXPECT_TRUE(ws.documentPath.empty());
    EXPECT_TRUE(ws.selectedPath.empty());
    EXPECT_FALSE(ws.bDirty);
    EXPECT_TRUE(ws.flattenRows().empty());
}

} // namespace guiworkbench
