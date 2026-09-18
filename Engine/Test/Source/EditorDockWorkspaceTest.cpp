#include "GameEditor/UI/Dock/EditorDockWorkspace.h"
#include "GameEditor/UI/Shell/EditorTabSpawnerRegistry.h"

#include "GUI/Widgets/Controls/DockSpace/DockContext.h"
#include "GUI/Widgets/Controls/DockSpace/DockNode.h"
#include "GUI/Widgets/Controls/Panel.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <string>
#include <unordered_set>
#include <vector>

namespace ya
{

namespace
{

std::vector<std::string> leafKeys(const FDockContext& context, std::string_view panelKey)
{
    const FDockContext::FPanel* panel = context.findPanelByStableKey(panelKey);
    if (!panel) {
        return {};
    }
    const FDockNode* leaf = context.dockModel().findLeafForPanel(panel->id);
    if (!leaf) {
        return {};
    }
    std::vector<std::string> keys;
    for (const DockPanelId id : leaf->panelIds) {
        if (const FDockPanelRecord* record = context.dockModel().findPanel(id)) {
            keys.push_back(record->stableKey);
        }
    }
    return keys;
}

bool spawnLayoutPanels(FDockContext& context, const nlohmann::json& layout)
{
    for (const std::string& key : FDockContext::collectLayoutPanelKeys(layout)) {
        if (context.addPanel(key, key, std::make_shared<UICanvasPanel>(key)) == kInvalidDockPanelId) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST(EditorDockWorkspaceTest, FactoryLayoutPlacesDefaultTabs)
{
    FDockContext context;
    context.bAllowFloating = true;
    const nlohmann::json& factory = EditorDockWorkspace::factoryLayout();
    ASSERT_TRUE(spawnLayoutPanels(context, factory));
    ASSERT_TRUE(context.importLayoutJson(factory));

    EXPECT_EQ(context.dockModel().getRootNode()->kind, EDockNodeKind::Split);
    EXPECT_EQ(context.dockModel().getRootNode()->orientation, EDockSplitOrientation::Vertical);
    EXPECT_FLOAT_EQ(context.dockModel().getRootNode()->ratio, 0.78f);

    EXPECT_EQ(leafKeys(context, "level-editor"),
              (std::vector<std::string>{"level-editor", "ui-designer"}));
    EXPECT_EQ(leafKeys(context, "content-browser"),
              (std::vector<std::string>{"content-browser",
                                        "frame-stats",
                                        "runtime-tools",
                                        "render-settings",
                                        "asset-inspector",
                                        "debug-images"}));
    const FDockNode* pageLeaf = context.dockModel().findLeafForPanel(
        context.findPanelByStableKey("level-editor")->id);
    const FDockNode* toolsLeaf = context.dockModel().findLeafForPanel(
        context.findPanelByStableKey("content-browser")->id);
    ASSERT_NE(pageLeaf, nullptr);
    ASSERT_NE(toolsLeaf, nullptr);
    EXPECT_EQ(pageLeaf->leafRole, EDockLeafRole::Page);
    EXPECT_TRUE(pageLeaf->bHideTabBar);
    EXPECT_EQ(toolsLeaf->leafRole, EDockLeafRole::Tools);
    EXPECT_TRUE(context.floatingWindows().empty());
}

TEST(EditorDockWorkspaceTest, FactoryOwnedNestedLayoutPlacesOwnedTools)
{
    FDockContext context;
    context.bAllowFloating = true;
    const nlohmann::json& factory = EditorDockWorkspace::factoryOwnedNestedLayout();
    ASSERT_TRUE(spawnLayoutPanels(context, factory));
    ASSERT_TRUE(context.importLayoutJson(factory));

    EXPECT_EQ(context.dockModel().getRootNode()->kind, EDockNodeKind::Split);
    EXPECT_EQ(context.dockModel().getRootNode()->orientation, EDockSplitOrientation::Horizontal);
    EXPECT_FLOAT_EQ(context.dockModel().getRootNode()->ratio, 0.78f);

    EXPECT_EQ(leafKeys(context, "viewport"), std::vector<std::string>({"viewport"}));
    EXPECT_EQ(leafKeys(context, "play-toolbar"), std::vector<std::string>({"play-toolbar"}));
    EXPECT_EQ(leafKeys(context, "hierarchy"), std::vector<std::string>({"hierarchy"}));
    EXPECT_EQ(leafKeys(context, "inspector"), std::vector<std::string>({"inspector"}));
    ASSERT_NE(context.dockModel().getRootNode()->child[0].get(), nullptr);
    EXPECT_EQ(context.dockModel().getRootNode()->child[0]->orientation,
              EDockSplitOrientation::Horizontal);
    ASSERT_NE(context.dockModel().getRootNode()->child[0]->child[1].get(), nullptr);
    EXPECT_EQ(context.dockModel().getRootNode()->child[0]->child[1]->kind, EDockNodeKind::Split);
    EXPECT_EQ(context.dockModel().getRootNode()->child[0]->child[1]->orientation,
              EDockSplitOrientation::Horizontal);
    EXPECT_FLOAT_EQ(context.dockModel().getRootNode()->child[0]->child[1]->ratio, 0.0f);
    EXPECT_FLOAT_EQ(context.dockModel().getRootNode()->child[0]->child[1]->minExtent[0], 54.0f);
    EXPECT_TRUE(context.floatingWindows().empty());
}

TEST(EditorDockWorkspaceTest, LayoutDocumentForPlacementRemapsFlatV1)
{
    const nlohmann::json v1 = {
        {"version", 1},
        {"root",
         {{"kind", "leaf"},
          {"panels", nlohmann::json::array({"viewport", "content-browser"})},
          {"selected", "viewport"}}},
        {"floating", nlohmann::json::array()},
    };

    const nlohmann::json windowRoot =
        EditorDockWorkspace::layoutDocumentForPlacement(v1, EEditorTabPlacement::WindowRootDock);
    const nlohmann::json nested =
        EditorDockWorkspace::layoutDocumentForPlacement(v1, EEditorTabPlacement::EditorOwnedNested);
    EXPECT_EQ(windowRoot, EditorDockWorkspace::factoryLayout());
    EXPECT_EQ(nested, EditorDockWorkspace::factoryOwnedNestedLayout());
}

TEST(EditorDockWorkspaceTest, LayoutDocumentForPlacementV2SelectsFields)
{
    const nlohmann::json windowRoot = EditorDockWorkspace::factoryLayout();
    const nlohmann::json nested     = EditorDockWorkspace::factoryOwnedNestedLayout();
    const nlohmann::json v2 = {
        {"version", 2},
        {"windowRoot", windowRoot},
        {"ownedNested", nested},
    };
    EXPECT_EQ(EditorDockWorkspace::layoutDocumentForPlacement(v2, EEditorTabPlacement::WindowRootDock),
              windowRoot);
    EXPECT_EQ(EditorDockWorkspace::layoutDocumentForPlacement(v2, EEditorTabPlacement::EditorOwnedNested),
              nested);
}

TEST(EditorDockWorkspaceTest, LayoutDocumentForPlacementV3SelectsFields)
{
    const nlohmann::json windowRoot = EditorDockWorkspace::factoryLayout();
    const nlohmann::json nested     = EditorDockWorkspace::factoryOwnedNestedLayout();
    const nlohmann::json v3 = {
        {"version", 3},
        {"windowRoot", windowRoot},
        {"ownedNested", nested},
    };
    EXPECT_EQ(EditorDockWorkspace::layoutDocumentForPlacement(v3, EEditorTabPlacement::WindowRootDock),
              windowRoot);
    EXPECT_EQ(EditorDockWorkspace::layoutDocumentForPlacement(v3, EEditorTabPlacement::EditorOwnedNested),
              nested);
}

TEST(EditorDockWorkspaceTest, LayoutDocumentForPlacementV4SelectsMainWindowFields)
{
    const nlohmann::json windowRoot = EditorDockWorkspace::factoryLayout();
    const nlohmann::json nested     = EditorDockWorkspace::factoryOwnedNestedLayout();
    const nlohmann::json extraRoot  = {
        {"version", 1},
        {"root", {{"kind", "leaf"}, {"panels", nlohmann::json::array({"material-editor"})}, {"selected", "material-editor"}}},
        {"floating", nlohmann::json::array()},
        {"windows", nlohmann::json::array()},
    };
    const nlohmann::json v4 = {
        {"version", 4},
        {"windows",
         nlohmann::json::array({
             nlohmann::json{{"role", "tornOff"},
                            {"bounds", nlohmann::json::array({10, 20, 320, 240})},
                            {"windowRoot", extraRoot}},
             nlohmann::json{{"role", "main"},
                            {"bounds", nlohmann::json::array({0, 0, 1280, 720})},
                            {"monitor", 0},
                            {"maximized", false},
                            {"windowRoot", windowRoot},
                            {"ownedNested", nested}},
         })},
    };
    EXPECT_EQ(EditorDockWorkspace::layoutDocumentForPlacement(v4, EEditorTabPlacement::WindowRootDock),
              windowRoot);
    EXPECT_EQ(EditorDockWorkspace::layoutDocumentForPlacement(v4, EEditorTabPlacement::EditorOwnedNested),
              nested);
}

TEST(EditorDockWorkspaceTest, SavedLayoutWithUnknownSpawnerStillRestoresKnownTabs)
{
    FDockContext context;
    context.bAllowFloating = true;
    const nlohmann::json saved = {
        {"version", 1},
        {"root",
         {{"kind", "split"},
          {"orientation", "vertical"},
          {"ratio", 0.6f},
          {"children",
           nlohmann::json::array({
               nlohmann::json{{"kind", "leaf"},
                              {"panels", nlohmann::json::array({"viewport", "gui-workbench"})},
                              {"selected", "viewport"}},
               nlohmann::json{{"kind", "leaf"},
                              {"panels", nlohmann::json::array({"inspector"})},
                              {"selected", "inspector"}},
           })}}},
        {"floating", nlohmann::json::array()},
    };

    ASSERT_NE(context.addPanel("viewport", "Viewport", std::make_shared<UICanvasPanel>("V")), kInvalidDockPanelId);
    ASSERT_NE(context.addPanel("inspector", "Inspector", std::make_shared<UICanvasPanel>("I")), kInvalidDockPanelId);
    const nlohmann::json sanitized = FDockContext::sanitizeLayoutJson(
        saved, std::unordered_set<std::string>{"viewport", "inspector"});
    ASSERT_TRUE(context.importLayoutJson(sanitized));
    EXPECT_EQ(leafKeys(context, "viewport"), std::vector<std::string>({"viewport"}));
    EXPECT_EQ(leafKeys(context, "inspector"), std::vector<std::string>({"inspector"}));
    EXPECT_FALSE(context.hasPanel("gui-workbench"));
}

TEST(EditorDockWorkspaceTest, PageLeafRejectsToolsAndToolsLeafRejectsPages)
{
    EditorTabSpawnerRegistry registry;
    registerBuiltinEditorTabSpawners(registry);

    FDockContext context;
    context.bAllowFloating = true;
    EditorDockWorkspace workspace;
    workspace.bind(EditorDockWorkspace::FHost{
        .spawners        = &registry,
        .dock            = &context,
        .targetPlacement = EEditorTabPlacement::WindowRootDock,
    });

    const nlohmann::json& factory = EditorDockWorkspace::factoryLayout();
    ASSERT_TRUE(spawnLayoutPanels(context, factory));
    ASSERT_TRUE(context.importLayoutJson(factory));

    const DockNodeId pageLeaf  = context.dockModel().findFirstLeafWithRole(EDockLeafRole::Page);
    const DockNodeId toolsLeaf = context.dockModel().findFirstLeafWithRole(EDockLeafRole::Tools);
    ASSERT_NE(pageLeaf, kInvalidDockNodeId);
    ASSERT_NE(toolsLeaf, kInvalidDockNodeId);

    EXPECT_TRUE(context.acceptsLeafDrop("ui-designer", kUIEditorRootId, {}, pageLeaf, true));
    EXPECT_FALSE(context.acceptsLeafDrop("ui-designer", kUIEditorRootId, {}, pageLeaf, false));
    EXPECT_FALSE(context.acceptsLeafDrop("ui-designer", kUIEditorRootId, {}, toolsLeaf, true));
    EXPECT_TRUE(context.acceptsLeafDrop("content-browser", 0, {}, toolsLeaf, true));
    EXPECT_FALSE(context.acceptsLeafDrop("content-browser", 0, {}, pageLeaf, true));
    EXPECT_FALSE(context.acceptsLeafDrop("content-browser", 0, {}, pageLeaf, false));
    EXPECT_EQ(context.adoptLeafFor("script-editor", kScriptEditorRootId, {}), pageLeaf);
    EXPECT_EQ(context.adoptLeafFor("content-browser", 0, {}), toolsLeaf);
}

TEST(EditorDockWorkspaceTest, MaterializeTabInvokesSpawnCompletionAfterRegistration)
{
    EditorTabSpawnerRegistry registry;
    bool callbackCalled = false;
    bool panelPresentDuringCallback = false;
    FDockContext context;
    registry.add({
        .tabId = "callback-tab",
        .title = "Callback",
        .toolsMenuLabel = "Callback",
        .scope = EEditorTabScope::WindowTool,
        .spawn = [](FEditorTabSpawnContext&) {
            return std::make_shared<UICanvasPanel>("CallbackBody");
        },
        .onSpawnComplete = [&](FEditorTabSpawnContext&, UIElement&) {
            callbackCalled = true;
            panelPresentDuringCallback = context.hasPanel("callback-tab");
        },
    });

    context.bAllowFloating = true;
    EditorDockWorkspace workspace;
    workspace.bind(EditorDockWorkspace::FHost{
        .spawners        = &registry,
        .dock            = &context,
        .targetPlacement = EEditorTabPlacement::WindowRootDock,
    });

    ASSERT_TRUE(workspace.materializeTab("callback-tab"));
    EXPECT_TRUE(callbackCalled);
    EXPECT_TRUE(panelPresentDuringCallback);
    EXPECT_TRUE(context.hasPanel("callback-tab"));
}

TEST(EditorDockWorkspaceTest, RepairMovesWindowToolsOutOfPageLeaf)
{
    EditorTabSpawnerRegistry registry;
    registerBuiltinEditorTabSpawners(registry);

    FDockContext context;
    context.bAllowFloating = true;
    ASSERT_NE(context.addPanel("level-editor", "Level", std::make_shared<UICanvasPanel>("Level")),
              kInvalidDockPanelId);
    ASSERT_NE(context.addPanel("content-browser", "Content", std::make_shared<UICanvasPanel>("Content")),
              kInvalidDockPanelId);
    ASSERT_NE(context.addPanel("frame-stats", "Stats", std::make_shared<UICanvasPanel>("Stats")),
              kInvalidDockPanelId);

    FDockNode* root = context.dockModel().getRootNode();
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(root->kind, EDockNodeKind::Stack);
    ASSERT_TRUE(context.dockModel().setLeafRole(root->id, EDockLeafRole::Page));
    ASSERT_TRUE(context.dockModel().setHideTabBar(root->id, true));

    EditorDockWorkspace workspace;
    workspace.bind(EditorDockWorkspace::FHost{
        .spawners        = &registry,
        .dock            = &context,
        .targetPlacement = EEditorTabPlacement::WindowRootDock,
    });
    workspace.repairPlacement();

    const DockNodeId pageLeaf  = context.dockModel().findFirstLeafWithRole(EDockLeafRole::Page);
    const DockNodeId toolsLeaf = context.dockModel().findFirstLeafWithRole(EDockLeafRole::Tools);
    ASSERT_NE(pageLeaf, kInvalidDockNodeId);
    ASSERT_NE(toolsLeaf, kInvalidDockNodeId);
    EXPECT_EQ(leafKeys(context, "level-editor"), std::vector<std::string>({"level-editor"}));
    EXPECT_EQ(leafKeys(context, "content-browser"),
              (std::vector<std::string>{"content-browser", "frame-stats"}));
    EXPECT_EQ(context.dockModel().findLeafForPanel(context.findPanelByStableKey("content-browser")->id)->id,
              toolsLeaf);
}

TEST(EditorDockWorkspaceTest, RepairDoesNotCreateEmptyToolsWell)
{
    EditorTabSpawnerRegistry registry;
    registerBuiltinEditorTabSpawners(registry);

    FDockContext context;
    context.bAllowFloating = true;
    ASSERT_NE(context.addPanel("level-editor", "Level", std::make_shared<UICanvasPanel>("Level")),
              kInvalidDockPanelId);

    FDockNode* root = context.dockModel().getRootNode();
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(root->kind, EDockNodeKind::Stack);
    ASSERT_TRUE(context.dockModel().setLeafRole(root->id, EDockLeafRole::Page));
    ASSERT_TRUE(context.dockModel().setHideTabBar(root->id, true));

    EditorDockWorkspace workspace;
    workspace.bind(EditorDockWorkspace::FHost{
        .spawners        = &registry,
        .dock            = &context,
        .targetPlacement = EEditorTabPlacement::WindowRootDock,
    });
    workspace.repairPlacement();

    EXPECT_EQ(context.dockModel().leafIds().size(), 1u);
    EXPECT_NE(context.dockModel().findFirstLeafWithRole(EDockLeafRole::Page), kInvalidDockNodeId);
    EXPECT_EQ(context.dockModel().findFirstLeafWithRole(EDockLeafRole::Tools), kInvalidDockNodeId);
}

TEST(EditorDockWorkspaceTest, RepairPrunesExistingEmptyToolsWell)
{
    EditorTabSpawnerRegistry registry;
    registerBuiltinEditorTabSpawners(registry);

    FDockContext context;
    context.bAllowFloating = true;
    ASSERT_NE(context.addPanel("level-editor", "Level", std::make_shared<UICanvasPanel>("Level")),
              kInvalidDockPanelId);
    const DockNodeId pageId = context.dockModel().getRootNode()->id;
    ASSERT_TRUE(context.dockModel().setLeafRole(pageId, EDockLeafRole::Page));
    ASSERT_TRUE(context.dockModel().setHideTabBar(pageId, true));
    ASSERT_TRUE(context.dockModel().splitEmptyLeaf(pageId, EDockCardinalSide::South, 0.78f, true));
    FDockNode* split = context.dockModel().findNode(context.dockModel().getRootNode()->id);
    ASSERT_NE(split, nullptr);
    ASSERT_EQ(split->kind, EDockNodeKind::Split);
    ASSERT_TRUE(context.dockModel().setLeafRole(split->child[1]->id, EDockLeafRole::Tools));
    EXPECT_EQ(context.dockModel().leafIds().size(), 2u);

    EditorDockWorkspace workspace;
    workspace.bind(EditorDockWorkspace::FHost{
        .spawners        = &registry,
        .dock            = &context,
        .targetPlacement = EEditorTabPlacement::WindowRootDock,
    });
    workspace.repairPlacement();

    EXPECT_EQ(context.dockModel().leafIds().size(), 1u);
    EXPECT_NE(context.dockModel().findFirstLeafWithRole(EDockLeafRole::Page), kInvalidDockNodeId);
    EXPECT_EQ(context.dockModel().findFirstLeafWithRole(EDockLeafRole::Tools), kInvalidDockNodeId);
}

TEST(EditorDockWorkspaceTest, ToolsOnlyWindowAdoptsFullLeafWithoutPageSplit)
{
    EditorTabSpawnerRegistry registry;
    registerBuiltinEditorTabSpawners(registry);

    FDockContext context;
    context.bAllowFloating = true;
    EditorDockWorkspace workspace;
    workspace.bind(EditorDockWorkspace::FHost{
        .spawners        = &registry,
        .dock            = &context,
        .targetPlacement = EEditorTabPlacement::WindowRootDock,
    });

    const FDockNode* root = context.dockModel().getRootNode();
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(root->kind, EDockNodeKind::Stack);

    const DockNodeId leaf = context.adoptLeafFor("font-atlases", 0, {});
    EXPECT_EQ(leaf, root->id);
    EXPECT_EQ(context.dockModel().getRootNode()->kind, EDockNodeKind::Stack);
    EXPECT_EQ(context.dockModel().getRootNode()->leafRole, EDockLeafRole::Tools);
    EXPECT_TRUE(context.dockModel().getRootNode()->bHideTabBar);
    EXPECT_EQ(context.dockModel().leafIds().size(), 1u);
    EXPECT_EQ(context.dockModel().findFirstLeafWithRole(EDockLeafRole::Page), kInvalidDockNodeId);
}

TEST(EditorDockWorkspaceTest, NestedRepairPrunesEmptyGenericLeaf)
{
    FDockContext context;
    context.bAllowFloating = true;
    ASSERT_NE(context.addPanel("hierarchy", "Hierarchy", std::make_shared<UICanvasPanel>("H")),
              kInvalidDockPanelId);
    ASSERT_NE(context.addPanel("inspector", "Inspector", std::make_shared<UICanvasPanel>("I")),
              kInvalidDockPanelId);
    const DockNodeId rootId = context.dockModel().getRootNode()->id;
    ASSERT_TRUE(context.dockModel().splitEmptyLeaf(rootId, EDockCardinalSide::East, 0.5f, true));
    EXPECT_EQ(context.dockModel().leafIds().size(), 2u);

    EditorDockWorkspace workspace;
    workspace.bind(EditorDockWorkspace::FHost{
        .dock            = &context,
        .targetPlacement = EEditorTabPlacement::EditorOwnedNested,
    });
    workspace.repairPlacement();
    EXPECT_EQ(context.dockModel().leafIds().size(), 1u);
}

} // namespace ya
