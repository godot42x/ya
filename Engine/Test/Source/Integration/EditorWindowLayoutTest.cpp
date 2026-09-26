#include "GameEditor/UI/Dock/EditorWindowLayout.h"

#include "GUI/Host/GUIWindowManager.h"
#include "GUI/Widgets/Controls/DockSpace/DockContext.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GameEditor/UI/Dock/EditorDockWorkspace.h"
#include "GameEditor/UI/Shell/EditorDocumentSession.h"
#include "GameEditor/UI/Shell/EditorTabSpawnerRegistry.h"

#include <gtest/gtest.h>
#include <glm/glm.hpp>

namespace ya
{

namespace
{

struct EmptyDelegate final : IGUIAppDelegate
{
    void buildUI(WidgetTree&) override {}
};

} // namespace

TEST(EditorWindowLayoutTest, ExportDefaultSessionIsMainWindowEnvelope)
{
    EditorWindowRegistry windows;
    const nlohmann::json document = exportEditorWindowLayout(windows, nullptr, nullptr);
    EXPECT_EQ(document["version"], kEditorWindowLayoutVersion);
    ASSERT_TRUE(document["windows"].is_array());
    ASSERT_EQ(document["windows"].size(), 1u);
    EXPECT_EQ(document["windows"][0]["role"], "main");
    EXPECT_EQ(document["windows"][0]["windowId"], kDefaultEditorWindowId);
    EXPECT_TRUE(document["windows"][0].contains("windowRoot"));
    EXPECT_TRUE(document["windows"][0].contains("ownedNested"));
    EXPECT_TRUE(document["windows"][0].contains("bounds"));
    EXPECT_TRUE(document["windows"][0].contains("monitor"));
    EXPECT_TRUE(document["windows"][0].contains("maximized"));
    const nlohmann::json* main = findMainEditorWindowRecord(document);
    ASSERT_NE(main, nullptr);
    EXPECT_EQ((*main)["role"], "main");
}

TEST(EditorWindowLayoutTest, RestoreExtraCreatesCoordinatorWindowAndSession)
{
    EditorTabSpawnerRegistry spawners;
    spawners.add({
        .tabId          = "material-editor",
        .title          = "Material",
        .toolsMenuLabel = "Material",
        .scope          = EEditorTabScope::WindowRootEditor,
        .ownerEditorId  = kMaterialEditorRootId,
        .placement      = EEditorTabPlacement::WindowRootDock,
        .detachPolicy   = EEditorTabDetachPolicy::IndependentWindow,
        .spawn          = [](FEditorTabSpawnContext&) { return std::make_shared<UICanvasPanel>("M"); },
    });

    const nlohmann::json extraRoot = {
        {"version", 2},
        {"tree", {{"kind", "leaf"}, {"id", "main"}}},
        {"dockSpace",
         {{"main", {{"panels", nlohmann::json::array({"material-editor"})}, {"selected", "material-editor"}}}}},
        {"floating", nlohmann::json::array()},
        {"windows", nlohmann::json::array()},
    };
    const nlohmann::json document = {
        {"version", kEditorWindowLayoutVersion},
        {"windows",
         nlohmann::json::array({
             nlohmann::json{{"role", "main"},
                            {"windowId", kDefaultEditorWindowId},
                            {"bounds", nlohmann::json::array({0, 0, 800, 600})},
                            {"hasOrigin", false},
                            {"monitor", -1},
                            {"maximized", false},
                            {"windowRoot", EditorDockWorkspace::factoryLayout()},
                            {"ownedNested", EditorDockWorkspace::factoryOwnedNestedLayout()}},
             nlohmann::json{{"role", "tornOff"},
                            {"windowId", 2},
                            {"bounds", nlohmann::json::array({40, 50, 320, 240})},
                            {"hasOrigin", true},
                            {"monitor", 0},
                            {"maximized", false},
                            {"ownerEditorId", kMaterialEditorRootId},
                            {"activeRootId", kMaterialEditorRootId},
                            {"documentKey", "M.mat"},
                            {"windowRoot", extraRoot},
                            {"ownedNested", nlohmann::json::object()}},
         })},
    };

    EditorWindowRegistry windows;
    GUIWindowManager manager;
    ASSERT_TRUE(manager.init());
    EmptyDelegate content;
    FEditorNativeTearOff env{
        .coordinator = &manager,
        .content     = &content,
        .windows     = &windows,
        .spawners    = &spawners,
    };
    const size_t restored = restoreEditorExtraWindows(env, document);
    if (restored == 0 && manager.extraWindowCount() == 0) {
        GTEST_SKIP() << "SDL native window create failed";
    }
    EXPECT_EQ(restored, 1u);
    EXPECT_EQ(windows.extraCount(), 1u);
    EXPECT_EQ(manager.extraWindowCount(), 1u);
    EditorWindowSession* extra = windows.find(2);
    ASSERT_NE(extra, nullptr);
    ASSERT_NE(extra->surface().windowRootDock(), nullptr);
    EXPECT_TRUE(extra->surface().windowRootDock()->hasPanel("material-editor"));
    EXPECT_NE(extra->tree(), windows.defaultSession().tree());
    IGUIWindowSession* gui = manager.findSession(extra->hostGuiWindowId());
    ASSERT_NE(gui, nullptr);
    ASSERT_NE(gui->nativeWindow(), nullptr);
    int width = 0;
    int height = 0;
    gui->nativeWindow()->getWindowSize(width, height);
    EXPECT_EQ(width, 320);
    EXPECT_EQ(height, 240);
}

TEST(EditorWindowLayoutTest, RestoreDoesNotTreatOverlayFloatingPosAsScreen)
{
    EditorTabSpawnerRegistry spawners;
    spawners.add({
        .tabId          = "inspector",
        .title          = "Inspector",
        .toolsMenuLabel = "Inspector",
        .scope          = EEditorTabScope::EditorOwnedTool,
        .ownerEditorId  = kLevelEditorRootId,
        .placement      = EEditorTabPlacement::EditorOwnedNested,
        .detachPolicy   = EEditorTabDetachPolicy::TearOffKeepOwner,
        .spawn          = [](FEditorTabSpawnContext&) { return std::make_shared<UICanvasPanel>("I"); },
    });
    spawners.add({
        .tabId          = "viewport",
        .title          = "Viewport",
        .toolsMenuLabel = "Viewport",
        .scope          = EEditorTabScope::EditorOwnedTool,
        .ownerEditorId  = kLevelEditorRootId,
        .placement      = EEditorTabPlacement::EditorOwnedNested,
        .detachPolicy   = EEditorTabDetachPolicy::TearOffKeepOwner,
        .spawn          = [](FEditorTabSpawnContext&) { return std::make_shared<UICanvasPanel>("V"); },
    });

    const nlohmann::json nested = {
        {"version", 2},
        {"tree", {{"kind", "leaf"}, {"id", "main"}}},
        {"dockSpace", {{"main", {{"panels", nlohmann::json::array({"viewport"})}, {"selected", "viewport"}}}}},
        {"floating",
         nlohmann::json::array({nlohmann::json{{"panels", nlohmann::json::array({"inspector"})},
                                               {"pos", nlohmann::json::array({12.0f, 24.0f})},
                                               {"size", nlohmann::json::array({200.0f, 100.0f})}}})},
        {"windows", nlohmann::json::array()},
    };
    const nlohmann::json document = {
        {"version", kEditorWindowLayoutVersion},
        {"windows",
         nlohmann::json::array({
             nlohmann::json{{"role", "main"},
                            {"windowRoot", EditorDockWorkspace::factoryLayout()},
                            {"ownedNested", EditorDockWorkspace::factoryOwnedNestedLayout()}},
             nlohmann::json{{"role", "tornOff"},
                            {"bounds", nlohmann::json::array({100, 80, 400, 300})},
                            {"hasOrigin", true},
                            {"monitor", 0},
                            {"windowRoot", nlohmann::json::object()},
                            {"ownedNested", nested}},
         })},
    };

    EditorWindowRegistry windows;
    GUIWindowManager manager;
    ASSERT_TRUE(manager.init());
    EmptyDelegate content;
    FEditorNativeTearOff env{
        .coordinator = &manager,
        .content     = &content,
        .windows     = &windows,
        .spawners    = &spawners,
    };
    const size_t restored = restoreEditorExtraWindows(env, document);
    if (restored == 0) {
        GTEST_SKIP() << "SDL native window create failed";
    }
    ASSERT_EQ(windows.extraCount(), 1u);
    EditorWindowSession* extra = nullptr;
    windows.forEach([&](EditorWindowSession& session) {
        if (session.windowId() != kDefaultEditorWindowId) {
            extra = &session;
        }
    });
    ASSERT_NE(extra, nullptr);
    FDockContext* nestedDock = extra->surface().ownedNestedDock();
    ASSERT_NE(nestedDock, nullptr);
    ASSERT_EQ(nestedDock->floatingPlacements().size(), 1u);
    EXPECT_EQ(nestedDock->floatingPlacements().front().projection, EDockFloatingProjection::InProcessOverlay);
    EXPECT_EQ(nestedDock->floatingPlacements().front().geometrySpace, EDockGeometrySpace::TreeLocal);
    EXPECT_EQ(nestedDock->floatingPlacements().front().pos, glm::vec2(12.0f, 24.0f));

    IGUIWindowSession* gui = manager.findSession(extra->hostGuiWindowId());
    ASSERT_NE(gui, nullptr);
    ASSERT_NE(gui->nativeWindow(), nullptr);
    int x = 0;
    int y = 0;
    ASSERT_TRUE(gui->nativeWindow()->getWindowPosition(x, y));
    EXPECT_NE(glm::ivec2(x, y), glm::ivec2(12, 24));
    int width = 0;
    int height = 0;
    gui->nativeWindow()->getWindowSize(width, height);
    EXPECT_EQ(width, 400);
    EXPECT_EQ(height, 300);
}

TEST(EditorWindowLayoutTest, V3EnvelopeHasNoOsWindowsArray)
{
    const nlohmann::json v3 = {
        {"version", 3},
        {"windowRoot", EditorDockWorkspace::factoryLayout()},
        {"ownedNested", EditorDockWorkspace::factoryOwnedNestedLayout()},
    };
    EXPECT_EQ(findMainEditorWindowRecord(v3), nullptr);

    EditorWindowRegistry windows;
    GUIWindowManager manager;
    ASSERT_TRUE(manager.init());
    EmptyDelegate content;
    FEditorNativeTearOff env{
        .coordinator = &manager,
        .content     = &content,
        .windows     = &windows,
    };
    EXPECT_EQ(restoreEditorExtraWindows(env, v3), 0u);
    EXPECT_EQ(windows.extraCount(), 0u);
}

nlohmann::json makeMaterialExtraEnvelope(bool bClosing, uint32_t ownerEditorId, std::string documentKey)
{
    const nlohmann::json extraRoot = {
        {"version", 2},
        {"tree", {{"kind", "leaf"}, {"id", "main"}}},
        {"dockSpace",
         {{"main", {{"panels", nlohmann::json::array({"material-editor"})}, {"selected", "material-editor"}}}}},
        {"floating", nlohmann::json::array()},
        {"windows", nlohmann::json::array()},
    };
    nlohmann::json extra{
        {"role", "tornOff"},
        {"windowId", 2},
        {"bounds", nlohmann::json::array({12, 24, 320, 240})},
        {"hasOrigin", true},
        {"monitor", 999},
        {"maximized", false},
        {"ownerEditorId", ownerEditorId},
        {"activeRootId", ownerEditorId},
        {"documentKey", std::move(documentKey)},
        {"windowRoot", extraRoot},
        {"ownedNested", nlohmann::json::object()},
    };
    if (bClosing) {
        extra["closing"] = true;
    }
    return {
        {"version", kEditorWindowLayoutVersion},
        {"windows",
         nlohmann::json::array({
             nlohmann::json{{"role", "main"},
                            {"windowId", kDefaultEditorWindowId},
                            {"bounds", nlohmann::json::array({0, 0, 800, 600})},
                            {"hasOrigin", false},
                            {"monitor", -1},
                            {"maximized", false},
                            {"windowRoot", EditorDockWorkspace::factoryLayout()},
                            {"ownedNested", EditorDockWorkspace::factoryOwnedNestedLayout()}},
             extra,
         })},
    };
}

TEST(EditorWindowLayoutTest, RestoreSkipsClosingExtra)
{
    EditorTabSpawnerRegistry spawners;
    spawners.add({
        .tabId          = "material-editor",
        .title          = "Material",
        .toolsMenuLabel = "Material",
        .scope          = EEditorTabScope::WindowRootEditor,
        .ownerEditorId  = kMaterialEditorRootId,
        .placement      = EEditorTabPlacement::WindowRootDock,
        .detachPolicy   = EEditorTabDetachPolicy::IndependentWindow,
        .spawn          = [](FEditorTabSpawnContext&) { return std::make_shared<UICanvasPanel>("M"); },
    });
    EditorWindowRegistry windows;
    GUIWindowManager manager;
    ASSERT_TRUE(manager.init());
    EmptyDelegate content;
    FEditorNativeTearOff env{
        .coordinator = &manager,
        .content     = &content,
        .windows     = &windows,
        .spawners    = &spawners,
    };
    FEditorWindowRestoreStats stats;
    EXPECT_EQ(restoreEditorExtraWindows(env, makeMaterialExtraEnvelope(true, kMaterialEditorRootId, "M.mat"),
                                        &stats),
              0u);
    EXPECT_EQ(stats.skippedClosing, 1u);
    EXPECT_EQ(windows.extraCount(), 0u);
    EXPECT_EQ(manager.extraWindowCount(), 0u);
}

TEST(EditorWindowLayoutTest, RestoreSkipsMissingDocumentAndDoesNotRebind)
{
    EditorTabSpawnerRegistry spawners;
    spawners.add({
        .tabId          = "material-editor",
        .title          = "Material",
        .toolsMenuLabel = "Material",
        .scope          = EEditorTabScope::WindowRootEditor,
        .ownerEditorId  = kMaterialEditorRootId,
        .placement      = EEditorTabPlacement::WindowRootDock,
        .detachPolicy   = EEditorTabDetachPolicy::IndependentWindow,
        .spawn          = [](FEditorTabSpawnContext&) { return std::make_shared<UICanvasPanel>("M"); },
    });
    EditorDocumentRegistry documents;
    EditorDocumentSession* scene =
        documents.open(makeEditorSceneDocumentId("other.scene"), EEditorDocumentClosePolicy::Discard);
    ASSERT_NE(scene, nullptr);

    EditorWindowRegistry windows;
    GUIWindowManager manager;
    ASSERT_TRUE(manager.init());
    EmptyDelegate content;
    FEditorNativeTearOff env{
        .coordinator = &manager,
        .content     = &content,
        .windows     = &windows,
        .spawners    = &spawners,
        .documents   = &documents,
    };
    FEditorWindowRestoreStats stats;
    EXPECT_EQ(restoreEditorExtraWindows(env, makeMaterialExtraEnvelope(false, kMaterialEditorRootId, "M.mat"),
                                        &stats),
              0u);
    EXPECT_EQ(stats.skippedMissingDocument, 1u);
    EXPECT_EQ(windows.extraCount(), 0u);
    EXPECT_EQ(manager.extraWindowCount(), 0u);
    EXPECT_EQ(windows.defaultSession().root(kMaterialEditorRootId).document(), nullptr);
    EXPECT_EQ(windows.defaultSession().root(kLevelEditorRootId).document(), nullptr);
}

TEST(EditorWindowLayoutTest, RestoreSkipsUnknownOwner)
{
    EditorTabSpawnerRegistry spawners;
    spawners.add({
        .tabId          = "material-editor",
        .title          = "Material",
        .toolsMenuLabel = "Material",
        .scope          = EEditorTabScope::WindowRootEditor,
        .ownerEditorId  = kMaterialEditorRootId,
        .placement      = EEditorTabPlacement::WindowRootDock,
        .detachPolicy   = EEditorTabDetachPolicy::IndependentWindow,
        .spawn          = [](FEditorTabSpawnContext&) { return std::make_shared<UICanvasPanel>("M"); },
    });
    EditorWindowRegistry windows;
    GUIWindowManager manager;
    ASSERT_TRUE(manager.init());
    EmptyDelegate content;
    FEditorNativeTearOff env{
        .coordinator = &manager,
        .content     = &content,
        .windows     = &windows,
        .spawners    = &spawners,
    };
    FEditorWindowRestoreStats stats;
    EXPECT_EQ(restoreEditorExtraWindows(env, makeMaterialExtraEnvelope(false, 99u, "M.mat"), &stats), 0u);
    EXPECT_EQ(stats.skippedMissingOwner, 1u);
    EXPECT_EQ(windows.extraCount(), 0u);
}

TEST(EditorWindowLayoutTest, ExportOmitsEmptyTornOffWindow)
{
    EditorWindowRegistry windows;
    ASSERT_NE(windows.create(2), nullptr);
    const nlohmann::json document = exportEditorWindowLayout(windows, nullptr, nullptr);
    ASSERT_TRUE(document["windows"].is_array());
    ASSERT_EQ(document["windows"].size(), 1u);
    EXPECT_EQ(document["windows"][0]["role"], "main");
}

TEST(EditorWindowLayoutTest, ExportMarksClosingAndRestoreSkips)
{
    EditorTabSpawnerRegistry spawners;
    spawners.add({
        .tabId          = "material-editor",
        .title          = "Material",
        .toolsMenuLabel = "Material",
        .scope          = EEditorTabScope::WindowRootEditor,
        .ownerEditorId  = kMaterialEditorRootId,
        .placement      = EEditorTabPlacement::WindowRootDock,
        .detachPolicy   = EEditorTabDetachPolicy::IndependentWindow,
        .spawn          = [](FEditorTabSpawnContext&) { return std::make_shared<UICanvasPanel>("M"); },
    });
    const nlohmann::json source = makeMaterialExtraEnvelope(false, kMaterialEditorRootId, {});
    EditorWindowRegistry windows;
    GUIWindowManager manager;
    ASSERT_TRUE(manager.init());
    EmptyDelegate content;
    FEditorNativeTearOff env{
        .coordinator = &manager,
        .content     = &content,
        .windows     = &windows,
        .spawners    = &spawners,
    };
    const size_t restored = restoreEditorExtraWindows(env, source);
    if (restored == 0) {
        GTEST_SKIP() << "SDL native window create failed";
    }
    EditorWindowSession* extra = windows.find(2);
    ASSERT_NE(extra, nullptr);
    manager.requestClose(extra->hostGuiWindowId());
    const nlohmann::json exported = exportEditorWindowLayout(windows, nullptr, &manager);
    const nlohmann::json* torn = nullptr;
    for (const nlohmann::json& record : exported["windows"]) {
        if (record.is_object() && record.value("role", "") == "tornOff") {
            torn = &record;
        }
    }
    ASSERT_NE(torn, nullptr);
    EXPECT_TRUE(torn->value("closing", false));

    EditorWindowRegistry restoredWindows;
    GUIWindowManager restoredManager;
    ASSERT_TRUE(restoredManager.init());
    FEditorNativeTearOff restoredEnv{
        .coordinator = &restoredManager,
        .content     = &content,
        .windows     = &restoredWindows,
        .spawners    = &spawners,
    };
    FEditorWindowRestoreStats stats;
    EXPECT_EQ(restoreEditorExtraWindows(restoredEnv, exported, &stats), 0u);
    EXPECT_EQ(stats.skippedClosing, 1u);
    EXPECT_EQ(restoredWindows.extraCount(), 0u);
}

TEST(EditorWindowLayoutTest, RestoreRelocatesMissingMonitor)
{
    EditorTabSpawnerRegistry spawners;
    spawners.add({
        .tabId          = "material-editor",
        .title          = "Material",
        .toolsMenuLabel = "Material",
        .scope          = EEditorTabScope::WindowRootEditor,
        .ownerEditorId  = kMaterialEditorRootId,
        .placement      = EEditorTabPlacement::WindowRootDock,
        .detachPolicy   = EEditorTabDetachPolicy::IndependentWindow,
        .spawn          = [](FEditorTabSpawnContext&) { return std::make_shared<UICanvasPanel>("M"); },
    });
    EditorWindowRegistry windows;
    GUIWindowManager manager;
    ASSERT_TRUE(manager.init());
    EmptyDelegate content;
    FEditorNativeTearOff env{
        .coordinator = &manager,
        .content     = &content,
        .windows     = &windows,
        .spawners    = &spawners,
    };
    FEditorWindowRestoreStats stats;
    const size_t restored =
        restoreEditorExtraWindows(env, makeMaterialExtraEnvelope(false, kMaterialEditorRootId, {}), &stats);
    if (restored == 0) {
        GTEST_SKIP() << "SDL native window create failed";
    }
    EXPECT_EQ(stats.relocatedMonitor, 1u);
    EditorWindowSession* extra = windows.find(2);
    ASSERT_NE(extra, nullptr);
    IGUIWindowSession* gui = manager.findSession(extra->hostGuiWindowId());
    ASSERT_NE(gui, nullptr);
    ASSERT_NE(gui->nativeWindow(), nullptr);
    int x = 0;
    int y = 0;
    ASSERT_TRUE(gui->nativeWindow()->getWindowPosition(x, y));
    EXPECT_NE(glm::ivec2(x, y), glm::ivec2(12, 24));
}

} // namespace ya
