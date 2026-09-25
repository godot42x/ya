#include "GameEditor/UI/Shell/EditorRootSession.h"
#include "GameEditor/UI/Shell/EditorWindowRegistry.h"
#include "GameEditor/UI/Shell/EditorWindowSession.h"
#include "GameEditor/UI/Dock/EditorDockWorkspace.h"
#include "GameEditor/UI/Shell/EditorTabSpawnerRegistry.h"

#include "GUI/Binding/UndoStack.h"
#include "GUI/Widgets/Controls/DockSpace/DockContext.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/UIElement.h"

#include <cstdint>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

namespace ya
{

TEST(EditorRootSessionTest, OwnedToolDocksOnlyUnderItsRoot)
{
    const FEditorTabOwnership owned{
        .scope          = EEditorTabScope::EditorOwnedTool,
        .ownerEditorId = kLevelEditorRootId,
    };
    // An owned tool lives in exactly one place: its owner's dock. It is not a
    // window-level panel, so it never lands in the window root (the page well)
    // and never in another editor's dock.
    EXPECT_FALSE(canSpawnEditorTab(owned, EEditorTabPlacement::WindowRootDock, kLevelEditorRootId));
    EXPECT_TRUE(canSpawnEditorTab(owned, EEditorTabPlacement::EditorOwnedNested, kLevelEditorRootId));
    EXPECT_FALSE(canSpawnEditorTab(owned, EEditorTabPlacement::EditorOwnedNested, 99));
    EXPECT_FALSE(canSpawnEditorTab(owned, EEditorTabPlacement::WindowRootDock, 99));
    EXPECT_FALSE(canSpawnEditorTab(FEditorTabOwnership{.scope = EEditorTabScope::EditorOwnedTool,
                                                       .ownerEditorId = kInvalidEditorRootId},
                                   EEditorTabPlacement::WindowRootDock,
                                   kLevelEditorRootId));
}

TEST(EditorRootSessionTest, OnlyRootEditorsOccupyThePageWell)
{
    // The window root is the page well, so it takes pages only -- that is what
    // makes a page switch replace the whole workspace.
    EXPECT_TRUE(canSpawnEditorTab({.scope = EEditorTabScope::WindowRootEditor},
                                  EEditorTabPlacement::WindowRootDock,
                                  kLevelEditorRootId));
    EXPECT_TRUE(canSpawnEditorTab({.scope = EEditorTabScope::WindowRootEditor},
                                  EEditorTabPlacement::WindowPageTab,
                                  kLevelEditorRootId));
    EXPECT_FALSE(canSpawnEditorTab({.scope = EEditorTabScope::WindowRootEditor},
                                   EEditorTabPlacement::EditorOwnedNested,
                                   kLevelEditorRootId));
    // An owned tool never becomes a page.
    EXPECT_FALSE(canSpawnEditorTab(
        {.scope = EEditorTabScope::EditorOwnedTool, .ownerEditorId = kUIEditorRootId},
        EEditorTabPlacement::WindowPageTab,
        kLevelEditorRootId));
}

TEST(EditorRootSessionTest, TearOffAndDropPolicyFollowDetachAndScope)
{
    const FEditorTabDragPayload locked{
        .tabId         = "level-editor",
        .scope         = EEditorTabScope::WindowRootEditor,
        .ownerEditorId = kLevelEditorRootId,
        .detachPolicy  = EEditorTabDetachPolicy::Locked,
    };
    EXPECT_FALSE(canTearOffEditorTab(locked));
    EXPECT_TRUE(canAcceptEditorDrop(locked, EEditorTabPlacement::WindowRootDock, kLevelEditorRootId));
    EXPECT_FALSE(canAcceptEditorDrop(locked, EEditorTabPlacement::EditorOwnedNested, kLevelEditorRootId));

    const FEditorTabDragPayload owned{
        .tabId         = "hierarchy",
        .scope         = EEditorTabScope::EditorOwnedTool,
        .ownerEditorId = kLevelEditorRootId,
        .detachPolicy  = EEditorTabDetachPolicy::TearOffKeepOwner,
        .sourcePlacement = EEditorTabPlacement::EditorOwnedNested,
    };
    EXPECT_TRUE(canTearOffEditorTab(owned));
    EXPECT_TRUE(canAcceptEditorDrop(owned, EEditorTabPlacement::EditorOwnedNested, kLevelEditorRootId));
    EXPECT_FALSE(canAcceptEditorDrop(owned, EEditorTabPlacement::EditorOwnedNested, kUIEditorRootId));
    // A tool belongs to its owner's dock only: the page well is not a place a
    // tool panel can be dropped into.
    EXPECT_FALSE(canAcceptEditorDrop(owned, EEditorTabPlacement::WindowRootDock, kLevelEditorRootId));
    EXPECT_FALSE(canAcceptEditorDrop(owned, EEditorTabPlacement::WindowPageTab, kLevelEditorRootId));

    const FEditorTabDragPayload root{
        .tabId         = "material-editor",
        .scope         = EEditorTabScope::WindowRootEditor,
        .ownerEditorId = kMaterialEditorRootId,
        .detachPolicy  = EEditorTabDetachPolicy::IndependentWindow,
    };
    EXPECT_TRUE(canTearOffEditorTab(root));
    EXPECT_TRUE(canAcceptEditorDrop(root, EEditorTabPlacement::WindowRootDock, kLevelEditorRootId));
    EXPECT_TRUE(canAcceptEditorDrop(root, EEditorTabPlacement::WindowPageTab, kLevelEditorRootId));
    EXPECT_FALSE(canAcceptEditorDrop(root, EEditorTabPlacement::EditorOwnedNested, kMaterialEditorRootId));
    EXPECT_TRUE(canRedockEditorTab(owned));
    EXPECT_TRUE(canRedockEditorTab(root));
    EXPECT_FALSE(canRedockEditorTab(locked));
    EXPECT_FALSE(canCloseEditorWindow(kDefaultEditorWindowId));
    EXPECT_FALSE(canCloseEditorWindow(kInvalidEditorWindowId));
    EXPECT_TRUE(canCloseEditorWindow(2));
}

TEST(EditorDockWorkspaceTest, WindowRootRefusesLevelOwnedTool)
{
    EditorTabSpawnerRegistry spawners;
    spawners.add({
        .tabId          = "hierarchy",
        .title          = "Hierarchy",
        .toolsMenuLabel = "Hierarchy",
        .scope          = EEditorTabScope::EditorOwnedTool,
        .ownerEditorId  = kLevelEditorRootId,
        .placement      = EEditorTabPlacement::EditorOwnedNested,
        .detachPolicy   = EEditorTabDetachPolicy::TearOffKeepOwner,
        .spawn          = [](FEditorTabSpawnContext&) { return std::make_shared<UICanvasPanel>("H"); },
    });

    FDockContext nested;
    nested.sourceScope = EDockSourceScope::EditorOwned;
    FDockContext windowRoot;
    windowRoot.sourceScope = EDockSourceScope::WindowRoot;
    auto widget = std::make_shared<UICanvasPanel>("H");
    const DockPanelId id = nested.addPanel("hierarchy", "Hierarchy", widget);
    ASSERT_NE(id, kInvalidDockPanelId);
    ASSERT_TRUE(nested.setPanelIdentity(id, kLevelEditorRootId, "scene.yascene"));

    EditorDockWorkspace workspace;
    workspace.bind({
        .spawners        = &spawners,
        .dock            = &windowRoot,
        .activeRootId    = kLevelEditorRootId,
        .targetPlacement = EEditorTabPlacement::WindowRootDock,
    });
    const DockPanelId moved = nested.transferPanelTo(windowRoot, id);
    // The window root is the page well. A Level-owned tool that a user drags
    // over it must stay where it is rather than appear as a page.
    EXPECT_EQ(moved, kInvalidDockPanelId);
    EXPECT_EQ(windowRoot.findPanelByStableKey("hierarchy"), nullptr);
    ASSERT_NE(nested.findPanel(id), nullptr);
    EXPECT_EQ(nested.findPanel(id)->widget, widget);
}

TEST(EditorDockWorkspaceTest, LevelNestedAdoptPolicyAcceptsLevelOwnedTool)
{
    EditorTabSpawnerRegistry spawners;
    spawners.add({
        .tabId          = "content-browser",
        .title          = "Content",
        .toolsMenuLabel = "Content",
        .scope          = EEditorTabScope::EditorOwnedTool,
        .ownerEditorId  = kLevelEditorRootId,
        .spawn          = [](FEditorTabSpawnContext&) { return std::make_shared<UICanvasPanel>("C"); },
    });

    FDockContext windowRoot;
    windowRoot.sourceScope = EDockSourceScope::WindowRoot;
    FDockContext nested;
    nested.sourceScope = EDockSourceScope::EditorOwned;
    auto widget = std::make_shared<UICanvasPanel>("C");
    const DockPanelId id = windowRoot.addPanel("content-browser", "Content", widget);
    ASSERT_NE(id, kInvalidDockPanelId);

    EditorDockWorkspace workspace;
    workspace.bind({
        .spawners        = &spawners,
        .dock            = &nested,
        .activeRootId    = kLevelEditorRootId,
        .targetPlacement = EEditorTabPlacement::EditorOwnedNested,
    });
    const DockPanelId moved = windowRoot.transferPanelTo(nested, id);
    EXPECT_NE(moved, kInvalidDockPanelId);
    EXPECT_EQ(windowRoot.findPanel(id), nullptr);
    ASSERT_NE(nested.findPanel(moved), nullptr);
    EXPECT_EQ(nested.findPanel(moved)->widget, widget);
}

TEST(EditorDockWorkspaceTest, LockedTabCannotImportIntoAnotherWindowRoot)
{
    EditorTabSpawnerRegistry spawners;
    spawners.add({
        .tabId          = "level-editor",
        .title          = "Level",
        .toolsMenuLabel = "Level",
        .scope          = EEditorTabScope::WindowRootEditor,
        .ownerEditorId  = kLevelEditorRootId,
        .placement      = EEditorTabPlacement::WindowRootDock,
        .detachPolicy   = EEditorTabDetachPolicy::Locked,
        .spawn          = [](FEditorTabSpawnContext&) { return std::make_shared<UICanvasPanel>("L"); },
    });

    FDockContext source;
    source.sourceScope = EDockSourceScope::WindowRoot;
    FDockContext extraRoot;
    extraRoot.sourceScope = EDockSourceScope::WindowRoot;
    auto widget = std::make_shared<UICanvasPanel>("L");
    const DockPanelId id = source.addPanel("level-editor", "Level", widget);
    ASSERT_NE(id, kInvalidDockPanelId);
    ASSERT_TRUE(source.setPanelIdentity(id, kLevelEditorRootId, "scene.yascene"));

    EditorDockWorkspace extraWorkspace;
    extraWorkspace.bind({
        .spawners        = &spawners,
        .dock            = &extraRoot,
        .activeRootId    = kLevelEditorRootId,
        .targetPlacement = EEditorTabPlacement::WindowRootDock,
        .windowId        = 2,
    });
    EXPECT_EQ(source.transferPanelTo(extraRoot, id), kInvalidDockPanelId);
    ASSERT_NE(source.findPanel(id), nullptr);
    EXPECT_EQ(source.findPanel(id)->widget, widget);
    EXPECT_EQ(extraRoot.findPanelByStableKey("level-editor"), nullptr);
}

TEST(EditorRootSessionTest, WindowSessionReferencesLevelRoot)
{
    EditorWindowSession session;
    EXPECT_EQ(session.activeRoot().id(), kLevelEditorRootId);
    SelectionModel& selection = session.activeRoot().selection();
    session.activeRoot().selection().select("entity-1");
    EXPECT_EQ(&session.activeRoot().selection(), &selection);
    EXPECT_TRUE(session.activeRoot().selection().contains("entity-1"));
}

TEST(EditorTabSpawnerRegistryTest, BuiltinOwnedToolsBindToLevelEditor)
{
    EditorTabSpawnerRegistry registry;
    registerBuiltinEditorTabSpawners(registry);

    const FEditorTabSpawner* level = registry.find("level-editor");
    ASSERT_NE(level, nullptr);
    EXPECT_EQ(level->scope, EEditorTabScope::WindowRootEditor);
    EXPECT_EQ(level->ownerEditorId, kLevelEditorRootId);
    EXPECT_EQ(level->placement, EEditorTabPlacement::WindowRootDock);
    EXPECT_EQ(level->detachPolicy, EEditorTabDetachPolicy::Locked);

    const FEditorTabSpawner* hierarchy = registry.find("hierarchy");
    ASSERT_NE(hierarchy, nullptr);
    EXPECT_EQ(hierarchy->scope, EEditorTabScope::EditorOwnedTool);
    EXPECT_EQ(hierarchy->ownerEditorId, kLevelEditorRootId);
    EXPECT_EQ(hierarchy->placement, EEditorTabPlacement::EditorOwnedNested);
    EXPECT_EQ(hierarchy->detachPolicy, EEditorTabDetachPolicy::TearOffKeepOwner);

    const FEditorTabSpawner* viewport = registry.find("viewport");
    ASSERT_NE(viewport, nullptr);
    EXPECT_EQ(viewport->scope, EEditorTabScope::EditorOwnedTool);
    EXPECT_EQ(viewport->ownerEditorId, kLevelEditorRootId);
    EXPECT_EQ(viewport->placement, EEditorTabPlacement::EditorOwnedNested);
    EXPECT_EQ(viewport->detachPolicy, EEditorTabDetachPolicy::Locked);

    const FEditorTabSpawner* play = registry.find("play-toolbar");
    ASSERT_NE(play, nullptr);
    EXPECT_EQ(play->scope, EEditorTabScope::EditorOwnedTool);
    EXPECT_EQ(play->ownerEditorId, kLevelEditorRootId);
    EXPECT_EQ(play->placement, EEditorTabPlacement::EditorOwnedNested);
    EXPECT_EQ(play->detachPolicy, EEditorTabDetachPolicy::TearOffKeepOwner);

    const FEditorTabSpawner* inspector = registry.find("inspector");
    ASSERT_NE(inspector, nullptr);
    EXPECT_EQ(inspector->scope, EEditorTabScope::EditorOwnedTool);
    EXPECT_EQ(inspector->ownerEditorId, kLevelEditorRootId);
    EXPECT_EQ(inspector->placement, EEditorTabPlacement::EditorOwnedNested);
    EXPECT_EQ(inspector->detachPolicy, EEditorTabDetachPolicy::TearOffKeepOwner);

    // Level's tool panels are Level-owned: they live in Level's own dock, not
    // in a window-level well, so they cannot outlive a page switch.
    const FEditorTabSpawner* content = registry.find("content-browser");
    ASSERT_NE(content, nullptr);
    EXPECT_EQ(content->scope, EEditorTabScope::EditorOwnedTool);
    EXPECT_EQ(content->ownerEditorId, kLevelEditorRootId);
    EXPECT_EQ(content->placement, EEditorTabPlacement::EditorOwnedNested);
    EXPECT_EQ(content->detachPolicy, EEditorTabDetachPolicy::IndependentWindow);

    const FEditorTabSpawner* designer = registry.find("ui-designer");
    ASSERT_NE(designer, nullptr);
    EXPECT_EQ(designer->scope, EEditorTabScope::WindowRootEditor);
    EXPECT_EQ(designer->ownerEditorId, kUIEditorRootId);
    EXPECT_EQ(designer->placement, EEditorTabPlacement::WindowRootDock);
    EXPECT_EQ(designer->detachPolicy, EEditorTabDetachPolicy::IndependentWindow);

    const FEditorTabSpawner* uiHierarchy = registry.find("ui-hierarchy");
    ASSERT_NE(uiHierarchy, nullptr);
    EXPECT_EQ(uiHierarchy->scope, EEditorTabScope::EditorOwnedTool);
    EXPECT_EQ(uiHierarchy->ownerEditorId, kUIEditorRootId);
    EXPECT_EQ(uiHierarchy->placement, EEditorTabPlacement::EditorOwnedNested);

    const FEditorTabSpawner* material = registry.find("material-editor");
    ASSERT_NE(material, nullptr);
    EXPECT_EQ(material->scope, EEditorTabScope::WindowRootEditor);
    EXPECT_EQ(material->ownerEditorId, kMaterialEditorRootId);

    const FEditorTabSpawner* script = registry.find("script-editor");
    ASSERT_NE(script, nullptr);
    EXPECT_EQ(script->scope, EEditorTabScope::WindowRootEditor);
    EXPECT_EQ(script->ownerEditorId, kScriptEditorRootId);

    const FEditorTabSpawner* matHierarchy = registry.find("material-hierarchy");
    ASSERT_NE(matHierarchy, nullptr);
    EXPECT_EQ(matHierarchy->scope, EEditorTabScope::EditorOwnedTool);
    EXPECT_EQ(matHierarchy->ownerEditorId, kMaterialEditorRootId);
}

TEST(EditorDockWorkspaceTest, MaterializeRejectsOwnedToolForOtherRoot)
{
    EditorTabSpawnerRegistry spawners;
    spawners.add({
        .tabId          = "hierarchy",
        .title          = "Hierarchy",
        .toolsMenuLabel = "Hierarchy",
        .scope          = EEditorTabScope::EditorOwnedTool,
        .ownerEditorId  = kLevelEditorRootId,
        .spawn          = [](FEditorTabSpawnContext&) { return std::make_shared<UICanvasPanel>("H"); },
    });

    FDockContext dock;
    EditorDockWorkspace workspace;
    workspace.bind({
        .spawners     = &spawners,
        .dock         = &dock,
        .activeRootId = 99,
    });
    EXPECT_FALSE(workspace.materializeTab("hierarchy"));
    EXPECT_FALSE(dock.hasPanel("hierarchy"));
}

TEST(EditorDockWorkspaceTest, MaterializeRejectsOwnedToolInOtherRootNestedDock)
{
    EditorTabSpawnerRegistry spawners;
    spawners.add({
        .tabId          = "hierarchy",
        .title          = "Hierarchy",
        .toolsMenuLabel = "Hierarchy",
        .scope          = EEditorTabScope::EditorOwnedTool,
        .ownerEditorId  = kLevelEditorRootId,
        .spawn          = [](FEditorTabSpawnContext&) { return std::make_shared<UICanvasPanel>("H"); },
    });

    FDockContext dock;
    EditorDockWorkspace workspace;
    workspace.bind({
        .spawners        = &spawners,
        .dock            = &dock,
        .activeRootId    = 99,
        .targetPlacement = EEditorTabPlacement::EditorOwnedNested,
    });
    EXPECT_FALSE(workspace.materializeTab("hierarchy"));
    EXPECT_FALSE(dock.hasPanel("hierarchy"));
}

TEST(EditorDockWorkspaceTest, MaterializeAcceptsOwnedToolInMatchingNestedDock)
{
    EditorTabSpawnerRegistry spawners;
    spawners.add({
        .tabId          = "hierarchy",
        .title          = "Hierarchy",
        .toolsMenuLabel = "Hierarchy",
        .scope          = EEditorTabScope::EditorOwnedTool,
        .ownerEditorId  = kLevelEditorRootId,
        .spawn          = [](FEditorTabSpawnContext&) { return std::make_shared<UICanvasPanel>("H"); },
    });

    FDockContext dock;
    EditorDockWorkspace workspace;
    workspace.bind({
        .spawners        = &spawners,
        .dock            = &dock,
        .activeRootId    = kLevelEditorRootId,
        .targetPlacement = EEditorTabPlacement::EditorOwnedNested,
    });
    EXPECT_TRUE(workspace.materializeTab("hierarchy"));
    EXPECT_TRUE(dock.hasPanel("hierarchy"));
}

TEST(EditorDockWorkspaceTest, MaterializeRejectsOwnedToolInWindowRootEvenMatchingOwner)
{
    EditorTabSpawnerRegistry spawners;
    spawners.add({
        .tabId          = "hierarchy",
        .title          = "Hierarchy",
        .toolsMenuLabel = "Hierarchy",
        .scope          = EEditorTabScope::EditorOwnedTool,
        .ownerEditorId  = kLevelEditorRootId,
        .spawn          = [](FEditorTabSpawnContext&) { return std::make_shared<UICanvasPanel>("H"); },
    });

    FDockContext dock;
    EditorDockWorkspace workspace;
    workspace.bind({
        .spawners        = &spawners,
        .dock            = &dock,
        .activeRootId    = kLevelEditorRootId,
        .targetPlacement = EEditorTabPlacement::WindowRootDock,
    });
    EXPECT_FALSE(workspace.materializeTab("hierarchy"));
    EXPECT_FALSE(dock.hasPanel("hierarchy"));
}

TEST(EditorDockWorkspaceTest, InvokeTabForwardsOwnedToolToNestedWorkspace)
{
    EditorTabSpawnerRegistry spawners;
    spawners.add({
        .tabId          = "hierarchy",
        .title          = "Hierarchy",
        .toolsMenuLabel = "Hierarchy",
        .scope          = EEditorTabScope::EditorOwnedTool,
        .ownerEditorId  = kLevelEditorRootId,
        .spawn          = [](FEditorTabSpawnContext&) { return std::make_shared<UICanvasPanel>("H"); },
    });

    FDockContext rootDock;
    FDockContext nestedDock;
    EditorDockWorkspace nestedWorkspace;
    nestedWorkspace.bind({
        .spawners        = &spawners,
        .dock            = &nestedDock,
        .activeRootId    = kLevelEditorRootId,
        .targetPlacement = EEditorTabPlacement::EditorOwnedNested,
    });
    EditorDockWorkspace rootWorkspace;
    rootWorkspace.bind({
        .spawners        = &spawners,
        .dock            = &rootDock,
        .activeRootId    = kLevelEditorRootId,
        .targetPlacement = EEditorTabPlacement::WindowRootDock,
        .nestedWorkspace = &nestedWorkspace,
    });
    EXPECT_TRUE(rootWorkspace.invokeTab("hierarchy"));
    EXPECT_TRUE(nestedDock.hasPanel("hierarchy"));
    EXPECT_FALSE(rootDock.hasPanel("hierarchy"));
}

TEST(EditorDockWorkspaceTest, InvokeTabActivatesNestedWindowToolWithoutSpawningDuplicate)
{
    EditorTabSpawnerRegistry spawners;
    spawners.add({
        .tabId          = "content-browser",
        .title          = "Content",
        .toolsMenuLabel = "Content Browser",
        .scope          = EEditorTabScope::EditorOwnedTool,
        .ownerEditorId  = kLevelEditorRootId,
        .spawn          = [](FEditorTabSpawnContext&) { return std::make_shared<UICanvasPanel>("C"); },
    });
    spawners.add({
        .tabId          = "hierarchy",
        .title          = "Hierarchy",
        .toolsMenuLabel = "Hierarchy",
        .scope          = EEditorTabScope::EditorOwnedTool,
        .ownerEditorId  = kLevelEditorRootId,
        .spawn          = [](FEditorTabSpawnContext&) { return std::make_shared<UICanvasPanel>("H"); },
    });

    FDockContext nestedDock;
    FDockContext rootDock;
    EditorDockWorkspace nestedWorkspace;
    nestedWorkspace.bind({
        .spawners        = &spawners,
        .dock            = &nestedDock,
        .activeRootId    = kLevelEditorRootId,
        .targetPlacement = EEditorTabPlacement::EditorOwnedNested,
    });
    EditorDockWorkspace rootWorkspace;
    rootWorkspace.bind({
        .spawners        = &spawners,
        .dock            = &rootDock,
        .activeRootId    = kLevelEditorRootId,
        .targetPlacement = EEditorTabPlacement::WindowRootDock,
        .nestedWorkspace = &nestedWorkspace,
    });

    const nlohmann::json layout = nlohmann::json::parse(R"JSON(
{
  "version": 2,
  "tree": { "kind": "leaf", "id": "main" },
  "dockSpace": {
    "main": { "panels": ["hierarchy", "content-browser"], "selected": "hierarchy" }
  },
  "floating": []
}
)JSON");
    ASSERT_TRUE(nestedWorkspace.applyLayoutDocument(layout, false));
    ASSERT_TRUE(nestedDock.hasPanel("content-browser"));
    ASSERT_FALSE(rootDock.hasPanel("content-browser"));

    EXPECT_TRUE(nestedWorkspace.invokeTab("content-browser"));
    EXPECT_TRUE(rootWorkspace.invokeTab("content-browser"));
    EXPECT_FALSE(rootDock.hasPanel("content-browser"));
    EXPECT_TRUE(nestedDock.hasPanel("content-browser"));
    const FDockContext::FPanel* panel = nestedDock.findPanelByStableKey("content-browser");
    ASSERT_NE(panel, nullptr);
    const FDockNode* leaf = nestedDock.dockModel().findLeafForPanel(panel->id);
    ASSERT_NE(leaf, nullptr);
    EXPECT_EQ(leaf->selectedPanel, panel->id);
}

TEST(EditorDockWorkspaceTest, InvokeTabOpensOwnedToolInItsOwnerDockNeverInThePageWell)
{
    EditorTabSpawnerRegistry spawners;
    spawners.add({
        .tabId          = "content-browser",
        .title          = "Content",
        .toolsMenuLabel = "Content Browser",
        .scope          = EEditorTabScope::EditorOwnedTool,
        .ownerEditorId  = kLevelEditorRootId,
        .placement      = EEditorTabPlacement::EditorOwnedNested,
        .spawn          = [](FEditorTabSpawnContext&) { return std::make_shared<UICanvasPanel>("C"); },
    });

    FDockContext nestedDock;
    FDockContext rootDock;
    EditorDockWorkspace nestedWorkspace;
    nestedWorkspace.bind({
        .spawners        = &spawners,
        .dock            = &nestedDock,
        .activeRootId    = kLevelEditorRootId,
        .targetPlacement = EEditorTabPlacement::EditorOwnedNested,
    });
    EditorDockWorkspace rootWorkspace;
    rootWorkspace.bind({
        .spawners        = &spawners,
        .dock            = &rootDock,
        .activeRootId    = kLevelEditorRootId,
        .targetPlacement = EEditorTabPlacement::WindowRootDock,
        .nestedWorkspace = &nestedWorkspace,
    });

    // Asked from the window root, the request forwards to Level's own dock: a
    // tool panel never materializes in the page well.
    EXPECT_TRUE(rootWorkspace.invokeTab("content-browser"));
    EXPECT_TRUE(nestedDock.hasPanel("content-browser"));
    EXPECT_FALSE(rootDock.hasPanel("content-browser"));
}

TEST(EditorDockWorkspaceTest, LockedTabRejectsClose)
{
    EditorTabSpawnerRegistry spawners;
    spawners.add({
        .tabId          = "level-editor",
        .title          = "Level",
        .toolsMenuLabel = "Level Editor",
        .scope          = EEditorTabScope::WindowRootEditor,
        .ownerEditorId  = kLevelEditorRootId,
        .placement      = EEditorTabPlacement::WindowRootDock,
        .detachPolicy   = EEditorTabDetachPolicy::Locked,
        .spawn          = [](FEditorTabSpawnContext& ctx) -> std::shared_ptr<UIElement> {
            if (!ctx.nestedDock) {
                return nullptr;
            }
            return std::make_shared<UICanvasPanel>("L");
        },
    });
    spawners.add({
        .tabId          = "viewport",
        .title          = "Viewport",
        .toolsMenuLabel = "Viewport",
        .scope          = EEditorTabScope::EditorOwnedTool,
        .ownerEditorId  = kLevelEditorRootId,
        .placement      = EEditorTabPlacement::EditorOwnedNested,
        .detachPolicy   = EEditorTabDetachPolicy::Locked,
        .spawn          = [](FEditorTabSpawnContext&) { return std::make_shared<UICanvasPanel>("V"); },
    });

    const std::shared_ptr<FDockContext> nested = std::make_shared<FDockContext>();
    FDockContext rootDock;
    EditorDockWorkspace nestedWorkspace;
    nestedWorkspace.bind({
        .spawners        = &spawners,
        .dock            = nested.get(),
        .activeRootId    = kLevelEditorRootId,
        .targetPlacement = EEditorTabPlacement::EditorOwnedNested,
    });
    EditorDockWorkspace rootWorkspace;
    rootWorkspace.bind({
        .spawners        = &spawners,
        .dock            = &rootDock,
        .activeRootId    = kLevelEditorRootId,
        .targetPlacement = EEditorTabPlacement::WindowRootDock,
        .nestedWorkspace = &nestedWorkspace,
        .nestedDock      = nested,
    });

    EXPECT_TRUE(rootWorkspace.materializeTab("level-editor"));
    EXPECT_TRUE(rootWorkspace.hasTab("level-editor"));
    EXPECT_FALSE(rootWorkspace.closeTab("level-editor"));
    EXPECT_TRUE(rootWorkspace.hasTab("level-editor"));

    EXPECT_TRUE(nestedWorkspace.materializeTab("viewport"));
    EXPECT_TRUE(nestedWorkspace.hasTab("viewport"));
    EXPECT_FALSE(nestedWorkspace.closeTab("viewport"));
    EXPECT_TRUE(nestedWorkspace.hasTab("viewport"));
}

TEST(EditorTabSpawnerRegistryTest, BuiltinLevelEditorSpawnRequiresNestedDock)
{
    EditorTabSpawnerRegistry registry;
    registerBuiltinEditorTabSpawners(registry);
    const FEditorTabSpawner* level = registry.find("level-editor");
    ASSERT_NE(level, nullptr);

    FEditorTabSpawnContext ctx;
    EXPECT_EQ(level->spawn(ctx), nullptr);
    ctx.nestedDock = std::make_shared<FDockContext>();
    EXPECT_NE(level->spawn(ctx), nullptr);
}

TEST(EditorDockWorkspaceTest, LayoutRestoresWindowToolInLevelNestedHost)
{
    EditorTabSpawnerRegistry spawners;
    spawners.add({
        .tabId          = "hierarchy",
        .title          = "Hierarchy",
        .toolsMenuLabel = "Hierarchy",
        .scope          = EEditorTabScope::EditorOwnedTool,
        .ownerEditorId  = kLevelEditorRootId,
        .spawn          = [](FEditorTabSpawnContext&) { return std::make_shared<UICanvasPanel>("H"); },
    });
    spawners.add({
        .tabId          = "content-browser",
        .title          = "Content",
        .toolsMenuLabel = "Content Browser",
        .scope          = EEditorTabScope::EditorOwnedTool,
        .ownerEditorId  = kLevelEditorRootId,
        .spawn          = [](FEditorTabSpawnContext&) { return std::make_shared<UICanvasPanel>("C"); },
    });

    FDockContext dock;
    EditorDockWorkspace workspace;
    workspace.bind({
        .spawners        = &spawners,
        .dock            = &dock,
        .activeRootId    = kLevelEditorRootId,
        .targetPlacement = EEditorTabPlacement::EditorOwnedNested,
    });

    const nlohmann::json layout = nlohmann::json::parse(R"JSON(
{
  "version": 2,
  "tree": { "kind": "leaf", "id": "main" },
  "dockSpace": {
    "main": { "panels": ["hierarchy", "content-browser"], "selected": "hierarchy" }
  },
  "floating": []
}
)JSON");
    EXPECT_TRUE(workspace.applyLayoutDocument(layout, false));
    EXPECT_TRUE(dock.hasPanel("hierarchy"));
    EXPECT_TRUE(dock.hasPanel("content-browser"));
}

TEST(EditorDockWorkspaceTest, WindowRootRestoreKeepsPagesAndDropsOwnedTools)
{
    EditorTabSpawnerRegistry spawners;
    spawners.add({
        .tabId          = "level-editor",
        .title          = "Level",
        .toolsMenuLabel = "Level Editor",
        .scope          = EEditorTabScope::WindowRootEditor,
        .ownerEditorId  = kLevelEditorRootId,
        .placement      = EEditorTabPlacement::WindowRootDock,
        .spawn          = [](FEditorTabSpawnContext&) { return std::make_shared<UICanvasPanel>("L"); },
    });
    spawners.add({
        .tabId          = "hierarchy",
        .title          = "Hierarchy",
        .toolsMenuLabel = "Hierarchy",
        .scope          = EEditorTabScope::EditorOwnedTool,
        .ownerEditorId  = kLevelEditorRootId,
        .spawn          = [](FEditorTabSpawnContext&) { return std::make_shared<UICanvasPanel>("H"); },
    });
    spawners.add({
        .tabId          = "content-browser",
        .title          = "Content",
        .toolsMenuLabel = "Content Browser",
        .scope          = EEditorTabScope::EditorOwnedTool,
        .ownerEditorId  = kLevelEditorRootId,
        .spawn          = [](FEditorTabSpawnContext&) { return std::make_shared<UICanvasPanel>("C"); },
    });

    FDockContext dock;
    EditorDockWorkspace workspace;
    workspace.bind({
        .spawners        = &spawners,
        .dock            = &dock,
        .activeRootId    = kLevelEditorRootId,
        .targetPlacement = EEditorTabPlacement::WindowRootDock,
    });

    const nlohmann::json layout = nlohmann::json::parse(R"JSON(
{
  "version": 2,
  "tree": { "kind": "leaf", "id": "main" },
  "dockSpace": {
    "main": { "panels": ["level-editor", "content-browser", "hierarchy"], "selected": "level-editor" }
  },
  "floating": []
}
)JSON");
    EXPECT_TRUE(workspace.applyLayoutDocument(layout, false));
    // The page well keeps its page and refuses the Level-owned tools a
    // pre-ownership layout had parked beside it.
    EXPECT_TRUE(dock.hasPanel("level-editor"));
    EXPECT_FALSE(dock.hasPanel("content-browser"));
    EXPECT_FALSE(dock.hasPanel("hierarchy"));
}

TEST(EditorDockWorkspaceTest, PreOwnershipWindowRootLayoutMigratesToPageWellOnly)
{
    // A layout saved before tools became editor-owned parks Level's panels in a
    // window-level tools leaf beside the page well. On restore that leaf must
    // disappear rather than keep showing another editor's tools.
    EditorTabSpawnerRegistry spawners;
    spawners.add({
        .tabId          = "level-editor",
        .title          = "Level",
        .toolsMenuLabel = "Level Editor",
        .scope          = EEditorTabScope::WindowRootEditor,
        .ownerEditorId  = kLevelEditorRootId,
        .placement      = EEditorTabPlacement::WindowRootDock,
        .spawn          = [](FEditorTabSpawnContext&) { return std::make_shared<UICanvasPanel>("L"); },
    });
    spawners.add({
        .tabId          = "ui-designer",
        .title          = "UI",
        .toolsMenuLabel = "UI Designer",
        .scope          = EEditorTabScope::WindowRootEditor,
        .ownerEditorId  = kUIEditorRootId,
        .placement      = EEditorTabPlacement::WindowRootDock,
        .spawn          = [](FEditorTabSpawnContext&) { return std::make_shared<UICanvasPanel>("U"); },
    });
    spawners.add({
        .tabId          = "content-browser",
        .title          = "Content",
        .toolsMenuLabel = "Content Browser",
        .scope          = EEditorTabScope::EditorOwnedTool,
        .ownerEditorId  = kLevelEditorRootId,
        .placement      = EEditorTabPlacement::EditorOwnedNested,
        .spawn          = [](FEditorTabSpawnContext&) { return std::make_shared<UICanvasPanel>("C"); },
    });
    spawners.add({
        .tabId          = "frame-stats",
        .title          = "Stats",
        .toolsMenuLabel = "Frame Stats",
        .scope          = EEditorTabScope::EditorOwnedTool,
        .ownerEditorId  = kLevelEditorRootId,
        .placement      = EEditorTabPlacement::EditorOwnedNested,
        .spawn          = [](FEditorTabSpawnContext&) { return std::make_shared<UICanvasPanel>("S"); },
    });

    FDockContext dock;
    EditorDockWorkspace workspace;
    workspace.bind({
        .spawners        = &spawners,
        .dock            = &dock,
        .activeRootId    = kLevelEditorRootId,
        .targetPlacement = EEditorTabPlacement::WindowRootDock,
    });

    const nlohmann::json legacy = nlohmann::json::parse(R"JSON(
{
  "version": 2,
  "tree": {
    "kind": "split",
    "orientation": "vertical",
    "ratio": 0.78,
    "minExtent": [120.0, 120.0],
    "children": [
      { "kind": "leaf", "id": "page" },
      { "kind": "leaf", "id": "tools" }
    ]
  },
  "dockSpace": {
    "page":  { "role": "page", "hideTabBar": true, "panels": ["level-editor", "ui-designer"], "selected": "ui-designer" },
    "tools": { "role": "tools", "panels": ["content-browser", "frame-stats"], "selected": "content-browser" }
  },
  "floating": []
}
)JSON");
    EXPECT_TRUE(workspace.applyLayoutDocument(legacy, false));

    EXPECT_TRUE(dock.hasPanel("level-editor"));
    EXPECT_TRUE(dock.hasPanel("ui-designer"));
    EXPECT_FALSE(dock.hasPanel("content-browser"));
    EXPECT_FALSE(dock.hasPanel("frame-stats"));
    EXPECT_EQ(dock.dockModel().findFirstLeafWithRole(EDockLeafRole::Tools), kInvalidDockNodeId);
    // Only the page well survives: the emptied tools leaf is pruned.
    EXPECT_EQ(dock.dockModel().leafIds().size(), 1u);
    EXPECT_NE(dock.dockModel().findFirstLeafWithRole(EDockLeafRole::Page), kInvalidDockNodeId);
}

TEST(EditorDockWorkspaceTest, LayoutDropsWindowToolFromUIOwnedNestedHost)
{
    EditorTabSpawnerRegistry spawners;
    spawners.add({
        .tabId          = "ui-hierarchy",
        .title          = "UI Tree",
        .toolsMenuLabel = "UI Tree",
        .scope          = EEditorTabScope::EditorOwnedTool,
        .ownerEditorId  = kUIEditorRootId,
        .placement      = EEditorTabPlacement::EditorOwnedNested,
        .spawn          = [](FEditorTabSpawnContext&) { return std::make_shared<UICanvasPanel>("U"); },
    });
    spawners.add({
        .tabId          = "content-browser",
        .title          = "Content",
        .toolsMenuLabel = "Content Browser",
        .scope          = EEditorTabScope::EditorOwnedTool,
        .ownerEditorId  = kLevelEditorRootId,
        .spawn          = [](FEditorTabSpawnContext&) { return std::make_shared<UICanvasPanel>("C"); },
    });

    FDockContext dock;
    EditorDockWorkspace workspace;
    workspace.bind({
        .spawners        = &spawners,
        .dock            = &dock,
        .activeRootId    = kUIEditorRootId,
        .targetPlacement = EEditorTabPlacement::EditorOwnedNested,
    });

    const nlohmann::json layout = nlohmann::json::parse(R"JSON(
{
  "version": 2,
  "tree": { "kind": "leaf", "id": "main" },
  "dockSpace": {
    "main": { "panels": ["ui-hierarchy", "content-browser"], "selected": "ui-hierarchy" }
  },
  "floating": []
}
)JSON");
    EXPECT_TRUE(workspace.applyLayoutDocument(layout, false));
    EXPECT_TRUE(dock.hasPanel("ui-hierarchy"));
    EXPECT_FALSE(dock.hasPanel("content-browser"));
}

TEST(EditorTabSpawnerRegistryTest, SpawnContextAllowsNullPointersAndNoSurface)
{
    FEditorTabSpawnContext ctx;
    EXPECT_EQ(ctx.windowId, kDefaultEditorWindowId);
    EXPECT_EQ(ctx.tree, nullptr);
    EXPECT_EQ(ctx.layer, nullptr);
    EXPECT_EQ(ctx.selection, nullptr);
    EXPECT_EQ(ctx.actions, nullptr);
    EXPECT_EQ(ctx.undo, nullptr);
    EXPECT_EQ(ctx.viewportHost, nullptr);
    EXPECT_FALSE(ctx.ownerEditorId.has_value());
    EXPECT_EQ(ctx.spawners, nullptr);
    EXPECT_EQ(ctx.documents, nullptr);
    EXPECT_EQ(ctx.app, nullptr);
    EXPECT_EQ(ctx.presentSurface, nullptr);
    EXPECT_EQ(ctx.ownerRoot, nullptr);
}

TEST(EditorTabSpawnerRegistryTest, RuntimeToolsSpawnWithNullTreeAndLayer)
{
    EditorTabSpawnerRegistry registry;
    registerBuiltinEditorTabSpawners(registry);
    const FEditorTabSpawner* spawner = registry.find("runtime-tools");
    ASSERT_NE(spawner, nullptr);
    FEditorTabSpawnContext ctx;
    const std::shared_ptr<UIElement> widget = spawner->spawn(ctx);
    ASSERT_NE(widget, nullptr);
}

TEST(EditorTabSpawnerRegistryTest, RenderSettingsSpawnWithNullTreeAndLayer)
{
    EditorTabSpawnerRegistry registry;
    registerBuiltinEditorTabSpawners(registry);
    const FEditorTabSpawner* spawner = registry.find("render-settings");
    ASSERT_NE(spawner, nullptr);
    EXPECT_EQ(spawner->toolsMenuLabel, "Render Settings");
    EXPECT_EQ(spawner->scope, EEditorTabScope::EditorOwnedTool);
    EXPECT_EQ(spawner->ownerEditorId, kLevelEditorRootId);
    FEditorTabSpawnContext ctx;
    const std::shared_ptr<UIElement> widget = spawner->spawn(ctx);
    ASSERT_NE(widget, nullptr);
}

TEST(EditorTabSpawnerRegistryTest, OwnedToolSpawnReturnsNullWithoutOwnerState)
{
    EditorTabSpawnerRegistry registry;
    registerBuiltinEditorTabSpawners(registry);
    FEditorTabSpawnContext ctx;

    const FEditorTabSpawner* hierarchy = registry.find("hierarchy");
    ASSERT_NE(hierarchy, nullptr);
    EXPECT_EQ(hierarchy->spawn(ctx), nullptr);

    const FEditorTabSpawner* inspector = registry.find("inspector");
    ASSERT_NE(inspector, nullptr);
    EXPECT_EQ(inspector->spawn(ctx), nullptr);
    SelectionModel selection;
    ctx.selection = &selection;
    EXPECT_EQ(inspector->spawn(ctx), nullptr);
}

TEST(EditorDockWorkspaceTest, MakeSpawnContextCopiesWindowAndSpawnerIdentity)
{
    FEditorTabSpawner spawner{
        .tabId          = "hierarchy",
        .title          = "Hierarchy",
        .toolsMenuLabel = "Hierarchy",
        .scope          = EEditorTabScope::EditorOwnedTool,
        .ownerEditorId  = kLevelEditorRootId,
        .placement      = EEditorTabPlacement::EditorOwnedNested,
        .detachPolicy   = EEditorTabDetachPolicy::TearOffKeepOwner,
        .spawn          = [](FEditorTabSpawnContext&) { return std::make_shared<UICanvasPanel>("H"); },
    };

    const std::shared_ptr<FDockContext> nestedDock = std::make_shared<FDockContext>();
    App* app = reinterpret_cast<App*>(static_cast<uintptr_t>(0x11));
    IRenderSurfaceContext* present = reinterpret_cast<IRenderSurfaceContext*>(static_cast<uintptr_t>(0x22));
    EditorDockWorkspace workspace;
    workspace.bind({
        .activeRootId    = kLevelEditorRootId,
        .windowId        = 7,
        .documentKey     = "scene.yascene",
        .nestedDock      = nestedDock,
        .app             = app,
        .presentSurface  = present,
    });
    const FEditorTabSpawnContext ctx = workspace.makeSpawnContext(spawner);
    EXPECT_EQ(ctx.windowId, 7u);
    EXPECT_EQ(ctx.scope, EEditorTabScope::EditorOwnedTool);
    ASSERT_TRUE(ctx.ownerEditorId.has_value());
    EXPECT_EQ(*ctx.ownerEditorId, kLevelEditorRootId);
    EXPECT_EQ(ctx.documentKey, "scene.yascene");
    EXPECT_EQ(ctx.placement, EEditorTabPlacement::EditorOwnedNested);
    EXPECT_EQ(ctx.detachPolicy, EEditorTabDetachPolicy::TearOffKeepOwner);
    EXPECT_EQ(ctx.tree, nullptr);
    EXPECT_EQ(ctx.layer, nullptr);
    EXPECT_EQ(ctx.selection, nullptr);
    EXPECT_EQ(ctx.nestedDock, nestedDock);
    EXPECT_EQ(ctx.app, app);
    EXPECT_EQ(ctx.presentSurface, present);
}

TEST(EditorDockWorkspaceTest, ToolOfAnotherRootDoesNotInheritHostDocumentKey)
{
    // The window's documentKey is the scene. A UI-owned tool is not about the
    // scene, so it must not be handed that key; its own root's document is the
    // only honest answer. This is the rule that keeps a page switch from
    // dragging another editor's document identity along with it.
    FEditorTabSpawner spawner{
        .tabId          = "ui-hierarchy",
        .title          = "UI Tree",
        .toolsMenuLabel = "UI Tree",
        .scope          = EEditorTabScope::EditorOwnedTool,
        .ownerEditorId  = kUIEditorRootId,
        .placement      = EEditorTabPlacement::EditorOwnedNested,
        .spawn          = [](FEditorTabSpawnContext&) { return std::make_shared<UICanvasPanel>("U"); },
    };

    EditorDockWorkspace workspace;
    workspace.bind({.documentKey = "scene.yascene", .activeRootId = kUIEditorRootId});
    const FEditorTabSpawnContext ctx = workspace.makeSpawnContext(spawner);
    EXPECT_TRUE(ctx.documentKey.empty());
    EXPECT_EQ(*ctx.ownerEditorId, kUIEditorRootId);
    EXPECT_EQ(ctx.placement, EEditorTabPlacement::EditorOwnedNested);
}

TEST(EditorDockWorkspaceTest, TwoSessionsOwnIndependentRootAndNestedDocks)
{
    EditorWindowRegistry windows;
    EditorWindowSession* extra = windows.create(2);
    ASSERT_NE(extra, nullptr);

    EditorTabSpawnerRegistry spawners;
    spawners.add({
        .tabId          = "hierarchy",
        .title          = "Hierarchy",
        .toolsMenuLabel = "Hierarchy",
        .scope          = EEditorTabScope::EditorOwnedTool,
        .ownerEditorId  = kLevelEditorRootId,
        .spawn          = [](FEditorTabSpawnContext&) { return std::make_shared<UICanvasPanel>("H"); },
    });
    spawners.add({
        .tabId          = "content-browser",
        .title          = "Content",
        .toolsMenuLabel = "Content Browser",
        .scope          = EEditorTabScope::EditorOwnedTool,
        .ownerEditorId  = kLevelEditorRootId,
        .spawn          = [](FEditorTabSpawnContext&) { return std::make_shared<UICanvasPanel>("C"); },
    });

    EditorWindowSession& primary = windows.defaultSession();
    FDockContext* nestedA = primary.surface().ownedNestedDock();
    FDockContext* nestedB = extra->surface().ownedNestedDock();
    FDockContext* rootA   = primary.surface().windowRootDock();
    FDockContext* rootB   = extra->surface().windowRootDock();
    ASSERT_NE(nestedA, nullptr);
    ASSERT_NE(nestedB, nullptr);
    ASSERT_NE(rootA, nullptr);
    ASSERT_NE(rootB, nullptr);
    EXPECT_NE(nestedA, nestedB);
    EXPECT_NE(rootA, rootB);

    EditorDockWorkspace nestedWorkspaceA;
    nestedWorkspaceA.bind({
        .spawners        = &spawners,
        .dock            = nestedA,
        .activeRootId    = primary.activeRoot().id(),
        .targetPlacement = EEditorTabPlacement::EditorOwnedNested,
        .windowId        = primary.windowId(),
    });
    EditorDockWorkspace nestedWorkspaceB;
    nestedWorkspaceB.bind({
        .spawners        = &spawners,
        .dock            = nestedB,
        .activeRootId    = extra->activeRoot().id(),
        .targetPlacement = EEditorTabPlacement::EditorOwnedNested,
        .windowId        = extra->windowId(),
    });
    EXPECT_TRUE(nestedWorkspaceA.materializeTab("hierarchy"));
    EXPECT_TRUE(nestedWorkspaceB.materializeTab("hierarchy"));
    EXPECT_TRUE(nestedA->hasPanel("hierarchy"));
    EXPECT_TRUE(nestedB->hasPanel("hierarchy"));
    // Both panels are Level-owned, so each window's own Level dock takes them;
    // neither window's page well does.
    EXPECT_TRUE(nestedWorkspaceA.materializeTab("content-browser"));
    EXPECT_TRUE(nestedWorkspaceB.materializeTab("content-browser"));
    EXPECT_TRUE(nestedA->hasPanel("content-browser"));
    EXPECT_TRUE(nestedB->hasPanel("content-browser"));
    EXPECT_FALSE(rootA->hasPanel("hierarchy"));
    EXPECT_FALSE(rootB->hasPanel("hierarchy"));

    EditorDockWorkspace rootWorkspaceB;
    rootWorkspaceB.bind({
        .spawners        = &spawners,
        .dock            = rootB,
        .activeRootId    = extra->activeRoot().id(),
        .targetPlacement = EEditorTabPlacement::WindowRootDock,
        .windowId        = extra->windowId(),
    });
    EXPECT_FALSE(rootWorkspaceB.materializeTab("content-browser"));
    EXPECT_FALSE(rootB->hasPanel("content-browser"));
    EXPECT_FALSE(rootA->hasPanel("content-browser"));
    EXPECT_FALSE(rootWorkspaceB.materializeTab("hierarchy"));
    EXPECT_FALSE(rootB->hasPanel("hierarchy"));
    const FEditorTabSpawner* content = spawners.find("content-browser");
    ASSERT_NE(content, nullptr);
    EXPECT_EQ(rootWorkspaceB.makeSpawnContext(*content).windowId, extra->windowId());
}

TEST(EditorRootSessionTest, UIOwnedToolCannotDockInLevelNested)
{
    EXPECT_FALSE(canSpawnEditorTab({.scope = EEditorTabScope::EditorOwnedTool,
                                    .ownerEditorId = kUIEditorRootId},
                                   EEditorTabPlacement::EditorOwnedNested,
                                   kLevelEditorRootId));
    EXPECT_TRUE(canSpawnEditorTab({.scope = EEditorTabScope::EditorOwnedTool,
                                   .ownerEditorId = kUIEditorRootId},
                                  EEditorTabPlacement::EditorOwnedNested,
                                  kUIEditorRootId));
    EXPECT_FALSE(canSpawnEditorTab({.scope = EEditorTabScope::EditorOwnedTool,
                                    .ownerEditorId = kMaterialEditorRootId},
                                   EEditorTabPlacement::EditorOwnedNested,
                                   kLevelEditorRootId));
}

TEST(EditorRootSessionTest, MaterialAndScriptRootsHaveIsolatedUndoAndSelection)
{
    EditorDocumentRegistry documents;
    EditorWindowSession session;
    EditorDocumentSession* material = documents.open({EEditorDocumentKind::Material, "a.mat"},
                                                     EEditorDocumentClosePolicy::RejectIfDirty);
    EditorDocumentSession* script = documents.open({EEditorDocumentKind::Script, "a.lua"},
                                                   EEditorDocumentClosePolicy::RejectIfDirty);
    ASSERT_NE(material, nullptr);
    ASSERT_NE(script, nullptr);
    session.root(kMaterialEditorRootId).bindDocument(material);
    session.root(kScriptEditorRootId).bindDocument(script);

    FUndoCommand command;
    command.label = "mat";
    command.undo  = [] {};
    command.redo  = [] {};
    EXPECT_TRUE(session.root(kMaterialEditorRootId).undo().push(std::move(command)));
    EXPECT_EQ(session.root(kMaterialEditorRootId).undo().undoCount(), 1u);
    EXPECT_EQ(session.root(kScriptEditorRootId).undo().undoCount(), 0u);
    EXPECT_EQ(session.activeRoot().undo().undoCount(), 0u);

    session.root(kMaterialEditorRootId).selection().select("mat-node");
    EXPECT_TRUE(session.root(kMaterialEditorRootId).selection().contains("mat-node"));
    EXPECT_FALSE(session.root(kScriptEditorRootId).selection().contains("mat-node"));
    EXPECT_FALSE(session.activeRoot().selection().contains("mat-node"));
}

TEST(EditorDockWorkspaceTest, UIOwnedToolCannotMaterializeInLevelNested)
{
    EditorTabSpawnerRegistry spawners;
    registerBuiltinEditorTabSpawners(spawners);
    FDockContext dock;
    EditorDockWorkspace workspace;
    workspace.bind({
        .spawners        = &spawners,
        .dock            = &dock,
        .activeRootId    = kLevelEditorRootId,
        .targetPlacement = EEditorTabPlacement::EditorOwnedNested,
    });
    EXPECT_FALSE(workspace.materializeTab("ui-hierarchy"));
    EXPECT_FALSE(dock.hasPanel("ui-hierarchy"));
    EXPECT_FALSE(workspace.materializeTab("material-hierarchy"));
    EXPECT_FALSE(dock.hasPanel("material-hierarchy"));
}

TEST(EditorDockWorkspaceTest, InvokeMaterialOwnedToolOpensRootThenNested)
{
    EditorTabSpawnerRegistry spawners;
    registerBuiltinEditorTabSpawners(spawners);
    FDockContext dock;
    EditorDockWorkspace workspace;
    workspace.bind({
        .spawners        = &spawners,
        .dock            = &dock,
        .activeRootId    = kLevelEditorRootId,
        .targetPlacement = EEditorTabPlacement::WindowRootDock,
    });
    EXPECT_TRUE(workspace.invokeTab("material-hierarchy"));
    EXPECT_TRUE(dock.hasPanel("material-editor"));
    EditorDockWorkspace* nested = workspace.nestedWorkspaceFor(kMaterialEditorRootId);
    ASSERT_NE(nested, nullptr);
    EXPECT_TRUE(nested->hasTab("material-hierarchy"));
    EXPECT_TRUE(nested->hasTab("material-preview"));
}

TEST(EditorDockWorkspaceTest, MaterialRootSpawnDoesNotInheritLevelNestedDock)
{
    FEditorTabSpawner spawner{
        .tabId          = "material-editor",
        .title          = "Material",
        .toolsMenuLabel = "Material Editor",
        .scope          = EEditorTabScope::WindowRootEditor,
        .ownerEditorId  = kMaterialEditorRootId,
        .placement      = EEditorTabPlacement::WindowRootDock,
        .detachPolicy   = EEditorTabDetachPolicy::IndependentWindow,
        .spawn          = [](FEditorTabSpawnContext&) { return std::make_shared<UICanvasPanel>("M"); },
    };
    const std::shared_ptr<FDockContext> nestedDock = std::make_shared<FDockContext>();
    EditorDockWorkspace workspace;
    workspace.bind({
        .activeRootId = kLevelEditorRootId,
        .nestedDock   = nestedDock,
    });
    const FEditorTabSpawnContext ctx = workspace.makeSpawnContext(spawner);
    EXPECT_EQ(ctx.nestedDock, nullptr);
    ASSERT_TRUE(ctx.ownerEditorId.has_value());
    EXPECT_EQ(*ctx.ownerEditorId, kMaterialEditorRootId);
}

TEST(EditorDockWorkspaceTest, FactoryOwnedNestedLayoutForUIDoesNotUseLevelTree)
{
    const nlohmann::json& ui = EditorDockWorkspace::factoryOwnedNestedLayoutFor(kUIEditorRootId);
    const std::vector<std::string> keys = FDockContext::collectLayoutPanelKeys(ui);
    bool hasPreview = false;
    bool hasHierarchy = false;
    bool hasLevelHierarchy = false;
    for (const std::string& key : keys) {
        hasPreview = hasPreview || key == "ui-preview";
        hasHierarchy = hasHierarchy || key == "ui-hierarchy";
        hasLevelHierarchy = hasLevelHierarchy || key == "hierarchy";
    }
    EXPECT_TRUE(hasPreview);
    EXPECT_TRUE(hasHierarchy);
    EXPECT_FALSE(hasLevelHierarchy);
}

TEST(EditorDockWorkspaceTest, MaterializeCopiesSpawnerIdentityOntoDockPanel)
{
    EditorTabSpawnerRegistry spawners;
    spawners.add({
        .tabId          = "hierarchy",
        .title          = "Hierarchy",
        .toolsMenuLabel = "Hierarchy",
        .scope          = EEditorTabScope::EditorOwnedTool,
        .ownerEditorId  = kLevelEditorRootId,
        .placement      = EEditorTabPlacement::EditorOwnedNested,
        .spawn          = [](FEditorTabSpawnContext&) { return std::make_shared<UICanvasPanel>("H"); },
    });
    FDockContext dock;
    dock.sourceScope  = EDockSourceScope::EditorOwned;
    dock.hostWindowId = 1;
    EditorDockWorkspace workspace;
    workspace.bind({
        .spawners        = &spawners,
        .dock            = &dock,
        .activeRootId    = kLevelEditorRootId,
        .targetPlacement = EEditorTabPlacement::EditorOwnedNested,
        .windowId        = 1,
        .documentKey     = "scene.yascene",
    });
    ASSERT_TRUE(workspace.materializeTab("hierarchy"));
    const FDockContext::FPanel* panel = dock.findPanelByStableKey("hierarchy");
    ASSERT_NE(panel, nullptr);
    EXPECT_EQ(panel->ownerEditorId, kLevelEditorRootId);
    EXPECT_EQ(panel->documentKey, "scene.yascene");
}

} // namespace ya
