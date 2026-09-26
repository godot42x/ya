#include "GameEditor/UI/Dock/EditorDockWorkspace.h"
#include "GameEditor/UI/Dock/EditorWindowLayout.h"
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

TEST(EditorDockWorkspaceTest, LayoutDocumentForPlacementSelectsMainWindowFields)
{
    const nlohmann::json windowRoot = EditorDockWorkspace::factoryLayout();
    const nlohmann::json nested     = EditorDockWorkspace::factoryOwnedNestedLayout();
    const nlohmann::json extraRoot   = {
        {"version", 2},
        {"tree", {{"kind", "leaf"}, {"id", "main"}}},
        {"dockSpace", {{"main", {{"panels", nlohmann::json::array({"material-editor"})}, {"selected", "material-editor"}}}}},
        {"floating", nlohmann::json::array()},
    };
    const nlohmann::json envelope = {
        {"version", kEditorWindowLayoutVersion},
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
    EXPECT_EQ(EditorDockWorkspace::layoutDocumentForPlacement(envelope, EEditorTabPlacement::WindowRootDock),
              windowRoot);
    EXPECT_EQ(EditorDockWorkspace::layoutDocumentForPlacement(envelope, EEditorTabPlacement::EditorOwnedNested),
              nested);
}

TEST(EditorDockWorkspaceTest, LayoutDocumentForPlacementFallsBackToFactoryOutsideEnvelope)
{
    // Legacy envelopes and bare dock documents are not mapped; both
    // placements fall back to the factory layouts.
    const nlohmann::json legacyEnvelope = {
        {"version", kEditorWindowLayoutVersion - 1},
        {"windowRoot", EditorDockWorkspace::factoryLayout()},
        {"ownedNested", EditorDockWorkspace::factoryOwnedNestedLayout()},
    };
    EXPECT_EQ(EditorDockWorkspace::layoutDocumentForPlacement(legacyEnvelope, EEditorTabPlacement::WindowRootDock),
              EditorDockWorkspace::factoryLayout());
    EXPECT_EQ(EditorDockWorkspace::layoutDocumentForPlacement(legacyEnvelope, EEditorTabPlacement::EditorOwnedNested),
              EditorDockWorkspace::factoryOwnedNestedLayout());

    const nlohmann::json bareDocument = {
        {"version", 2},
        {"tree", {{"kind", "leaf"}, {"id", "main"}}},
        {"dockSpace", {{"main", {{"panels", nlohmann::json::array({"viewport"})}}}}},
        {"floating", nlohmann::json::array()},
    };
    EXPECT_EQ(EditorDockWorkspace::layoutDocumentForPlacement(bareDocument, EEditorTabPlacement::WindowRootDock),
              EditorDockWorkspace::factoryLayout());
    EXPECT_EQ(EditorDockWorkspace::layoutDocumentForPlacement(bareDocument, EEditorTabPlacement::EditorOwnedNested),
              EditorDockWorkspace::factoryOwnedNestedLayout());
}

TEST(EditorDockWorkspaceTest, SavedLayoutWithUnknownSpawnerStillRestoresKnownTabs)
{
    FDockContext context;
    context.bAllowFloating = true;
    const nlohmann::json saved = {
        {"version", 2},
        {"tree",
         {{"kind", "split"},
          {"orientation", "vertical"},
          {"ratio", 0.6f},
          {"children",
           nlohmann::json::array({
               nlohmann::json{{"kind", "leaf"}, {"id", "page"}},
               nlohmann::json{{"kind", "leaf"}, {"id", "well"}},
           })}}},
        {"dockSpace",
         {
             {"page", {{"panels", nlohmann::json::array({"viewport", "gui-workbench"})}, {"selected", "viewport"}}},
             {"well", {{"panels", nlohmann::json::array({"inspector"})}, {"selected", "inspector"}}},
         }},
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

TEST(EditorDockWorkspaceTest, PageWellAcceptsPagesAndHasNoWindowLevelToolsLeaf)
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

    const nlohmann::json factory = EditorDockWorkspace::factoryLayout();
    ASSERT_TRUE(spawnLayoutPanels(context, factory));
    ASSERT_TRUE(context.importLayoutJson(factory));

    const DockNodeId pageLeaf  = context.dockModel().findFirstLeafWithRole(EDockLeafRole::Page);
    ASSERT_NE(pageLeaf, kInvalidDockNodeId);

    // A page merges into the page well on the center target only; a cardinal
    // split there would make a second page well the chrome cannot render.
    EXPECT_TRUE(context.acceptsLeafDrop("ui-designer", kUIEditorRootId, {}, pageLeaf, true));
    EXPECT_FALSE(context.acceptsLeafDrop("ui-designer", kUIEditorRootId, {}, pageLeaf, false));
    EXPECT_EQ(context.adoptLeafFor("script-editor", kScriptEditorRootId, {}), pageLeaf);

    // A Level-owned tool has no window-level home: it is refused by the page
    // well and there is no tools leaf for it to land in.
    EXPECT_FALSE(context.acceptsLeafDrop("content-browser", kLevelEditorRootId, {}, pageLeaf, true));
    EXPECT_FALSE(context.acceptsLeafDrop("content-browser", kLevelEditorRootId, {}, pageLeaf, false));
    EXPECT_EQ(context.dockModel().findFirstLeafWithRole(EDockLeafRole::Tools), kInvalidDockNodeId);
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
        .scope = EEditorTabScope::EditorOwnedTool,
        .ownerEditorId = kLevelEditorRootId,
        .placement = EEditorTabPlacement::EditorOwnedNested,
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
        .activeRootId    = kLevelEditorRootId,
        .targetPlacement = EEditorTabPlacement::EditorOwnedNested,
    });

    ASSERT_TRUE(workspace.materializeTab("callback-tab"));
    EXPECT_TRUE(callbackCalled);
    EXPECT_TRUE(panelPresentDuringCallback);
    EXPECT_TRUE(context.hasPanel("callback-tab"));
}

TEST(EditorDockWorkspaceTest, RepairClosesNonPagePanelsLeftInThePageWell)
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

    // A pre-ownership layout parked Level's tools in the page well. The page
    // well is pages-only and this object cannot re-home a panel into its
    // owner's dock, so the honest result is "closed", not "shown here".
    const DockNodeId pageLeaf = context.dockModel().findFirstLeafWithRole(EDockLeafRole::Page);
    ASSERT_NE(pageLeaf, kInvalidDockNodeId);
    EXPECT_EQ(leafKeys(context, "level-editor"), std::vector<std::string>({"level-editor"}));
    EXPECT_FALSE(context.hasPanel("content-browser"));
    EXPECT_FALSE(context.hasPanel("frame-stats"));
    EXPECT_EQ(context.dockModel().findFirstLeafWithRole(EDockLeafRole::Tools), kInvalidDockNodeId);
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

TEST(EditorDockWorkspaceTest, WindowRootWithoutPageWellRefusesTools)
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

    // A tool panel has no window-level home. A window root that has no page
    // well therefore refuses it instead of fabricating a tools leaf outside
    // the ownership model.
    EXPECT_EQ(context.adoptLeafFor("font-atlases", kLevelEditorRootId, {}), kInvalidDockNodeId);
    EXPECT_EQ(context.dockModel().leafIds().size(), 1u);
    EXPECT_EQ(context.dockModel().findFirstLeafWithRole(EDockLeafRole::Tools), kInvalidDockNodeId);
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
