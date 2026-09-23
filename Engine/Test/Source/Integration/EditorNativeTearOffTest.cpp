#include "GameEditor/UI/Dock/EditorNativeTearOff.h"

#include "Core/Event.h"
#include "GUI/Host/GUIWindowChrome.h"
#include "GUI/Host/GUIWindowManager.h"
#include "GUI/Widgets/Controls/DockSpace/DockContext.h"
#include "GUI/Widgets/Controls/DockSpace/DockSpace.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/TabBar.h"
#include "GUI/Widgets/UIElement.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GameEditor/UI/Shell/EditorDocumentSession.h"

#include <gtest/gtest.h>

namespace ya
{

namespace
{

struct EmptyDelegate final : IGUIAppDelegate
{
    void buildUI(WidgetTree&) override {}
};

void attachDock(WidgetTree& tree, const std::shared_ptr<FDockContext>& dock)
{
    auto space = std::make_shared<UIDockSpace>("SourceDock");
    FCanvasSlotArgs fill;
    fill.anchorMin = {0.0f, 0.0f};
    fill.anchorMax = {1.0f, 1.0f};
    (void)tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), space, fill);
    space->setContext(dock);
}

template <typename T>
T* findDescendantOfType(UIElement& root)
{
    if (auto* typed = dynamic_cast<T*>(&root)) {
        return typed;
    }
    for (const UIElementRef& child : root.getChildren()) {
        if (!child) {
            continue;
        }
        if (auto* typed = findDescendantOfType<T>(*child)) {
            return typed;
        }
    }
    return nullptr;
}

} // namespace

TEST(EditorNativeTearOffTest, LockedTabDoesNotOpenNativeWindow)
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

    EditorWindowRegistry windows;
    EditorWindowSession& source = windows.defaultSession();
    FDockContext* dock = source.surface().windowRootDock();
    ASSERT_NE(dock, nullptr);
    dock->bAllowFloating = true;
    dock->bAllowTearOff = true;
    auto widget = std::make_shared<UICanvasPanel>("L");
    const DockPanelId panelId = dock->addPanel("level-editor", "Level", widget);
    ASSERT_NE(panelId, kInvalidDockPanelId);
    ASSERT_TRUE(dock->setPanelIdentity(panelId, kLevelEditorRootId, "scene.yascene"));

    GUIWindowManager manager;
    ASSERT_TRUE(manager.init());
    EmptyDelegate content;
    FEditorNativeTearOff env{
        .coordinator = &manager,
        .content     = &content,
        .windows     = &windows,
        .spawners    = &spawners,
    };
    const FEditorTearOffResult result = tearOffEditorPanelToNativeWindow(env, source, *dock, panelId);
    EXPECT_EQ(result.editorWindowId, kInvalidEditorWindowId);
    EXPECT_EQ(result.guiWindowId, 0u);
    EXPECT_EQ(manager.extraWindowCount(), 0u);
    ASSERT_NE(dock->findPanel(panelId), nullptr);
    EXPECT_EQ(dock->findPanel(panelId)->widget, widget);
    EXPECT_EQ(dock->findPanel(panelId)->ownerEditorId, kLevelEditorRootId);
    EXPECT_EQ(dock->findPanel(panelId)->documentKey, "scene.yascene");
}

TEST(EditorNativeTearOffTest, HostEditorDockOnTreeDrawsTitleTabBar)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto dock = std::make_shared<FDockContext>();
    auto widget = std::make_shared<UICanvasPanel>("H");
    ASSERT_NE(dock->addPanel("hierarchy", "Hierarchy", widget), kInvalidDockPanelId);

    FWindowChromeLayout chrome;
    chrome.contentInsets.left = 78.0f;
    chrome.contentInsets.top  = 28.0f;
    chrome.dragRegion         = FWindowChromeRect{704.0f, 0.0f, 96.0f, 28.0f};
    hostEditorDockOnTree(tree, dock, chrome, nullptr);
    (void)tree.buildSnapshot(UIFrameBuildContext{});

    UIElement* layer = tree.getLayer(WidgetTree::ELayer::Content);
    ASSERT_NE(layer, nullptr);
    UITabBar* titleTabs = nullptr;
    UICanvasPanel* titleBar = nullptr;
    for (const UIElementRef& child : layer->getChildren()) {
        if (!child) {
            continue;
        }
        if (child->_name == "EditorTornPageTabs") {
            titleTabs = dynamic_cast<UITabBar*>(child.get());
        }
        if (child->_name == "EditorTornTitleBar") {
            titleBar = dynamic_cast<UICanvasPanel*>(child.get());
        }
    }
    ASSERT_NE(titleBar, nullptr);
    ASSERT_NE(titleTabs, nullptr);
    ASSERT_GE(titleTabs->tabCount(), 1);
    ASSERT_NE(titleTabs->tabAt(0), nullptr);
    EXPECT_EQ(titleTabs->tabAt(0)->_label, "Hierarchy");
}

TEST(EditorNativeTearOffTest, RootEditorMovesToNativeWindowWithoutDualMount)
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
    FEditorDocumentId materialId;
    materialId.kind = EEditorDocumentKind::Material;
    materialId.key = "M.mat";
    EditorDocumentSession* doc =
        documents.open(materialId, EEditorDocumentClosePolicy::Discard);
    ASSERT_NE(doc, nullptr);

    EditorWindowRegistry windows;
    EditorWindowSession& source = windows.defaultSession();
    source.root(kMaterialEditorRootId).bindDocument(doc);
    auto sourceDock = source.surface().windowRootDockPtr();
    ASSERT_NE(sourceDock, nullptr);
    sourceDock->bAllowFloating = true;
    sourceDock->bAllowTearOff = true;
    sourceDock->sourceScope = EDockSourceScope::WindowRoot;
    sourceDock->hostWindowId = source.windowId();

    WidgetTree sourceTree({.width = 400, .height = 300});
    attachDock(sourceTree, sourceDock);
    auto widget = std::make_shared<UICanvasPanel>("M");
    const DockPanelId panelId = sourceDock->addPanel("material-editor", "Material", widget);
    ASSERT_NE(panelId, kInvalidDockPanelId);
    ASSERT_TRUE(sourceDock->setPanelIdentity(panelId, kMaterialEditorRootId, "M.mat"));
    (void)sourceTree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_TRUE(sourceTree.contains(*widget));

    GUIWindowManager manager;
    ASSERT_TRUE(manager.init());
    EmptyDelegate content;
    FEditorNativeTearOff env{
        .coordinator = &manager,
        .content     = &content,
        .windows     = &windows,
        .spawners    = &spawners,
    };
    const FEditorTearOffResult result =
        tearOffEditorPanelToNativeWindow(env, source, *sourceDock, panelId);
    if (result.guiWindowId == 0) {
        GTEST_SKIP() << "SDL native window create failed";
    }

    EXPECT_NE(result.editorWindowId, kDefaultEditorWindowId);
    EditorWindowSession* extra = windows.find(result.editorWindowId);
    ASSERT_NE(extra, nullptr);
    IGUIWindowSession* gui = manager.findSession(result.guiWindowId);
    ASSERT_NE(gui, nullptr);
    ASSERT_NE(gui->tree(), nullptr);
    EXPECT_EQ(extra->tree(), gui->tree());
    EXPECT_NE(extra->tree(), source.tree());
    EXPECT_EQ(sourceDock->findPanel(panelId), nullptr);
    EXPECT_FALSE(sourceTree.contains(*widget));
    EXPECT_TRUE(gui->tree()->contains(*widget));
    EXPECT_EQ(widget->getTree(), gui->tree());

    const FDockContext::FPanel* moved =
        extra->surface().windowRootDock()->findPanel(result.targetPanelId);
    ASSERT_NE(moved, nullptr);
    EXPECT_EQ(moved->widget, widget);
    EXPECT_EQ(moved->ownerEditorId, kMaterialEditorRootId);
    EXPECT_EQ(moved->documentKey, "M.mat");
    EXPECT_EQ(extra->root(kMaterialEditorRootId).document(), doc);
    EXPECT_EQ(source.root(kMaterialEditorRootId).document(), doc);
    EXPECT_EQ(doc->bindCount(), 2);
}

TEST(EditorNativeTearOffTest, OwnedToolKeepsOwnerAndDocumentOnNativeWindow)
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

    EditorDocumentRegistry documents;
    EditorDocumentSession* doc =
        documents.open(makeEditorSceneDocumentId("scene.yascene"), EEditorDocumentClosePolicy::Discard);
    ASSERT_NE(doc, nullptr);

    EditorWindowRegistry windows;
    EditorWindowSession& source = windows.defaultSession();
    source.root(kLevelEditorRootId).bindDocument(doc);
    auto sourceDock = source.surface().ownedNestedDockPtr();
    ASSERT_NE(sourceDock, nullptr);
    sourceDock->bAllowFloating = true;
    sourceDock->bAllowTearOff = true;
    sourceDock->sourceScope = EDockSourceScope::EditorOwned;
    sourceDock->hostWindowId = source.windowId();

    WidgetTree sourceTree({.width = 400, .height = 300});
    attachDock(sourceTree, sourceDock);
    auto widget = std::make_shared<UICanvasPanel>("H");
    const DockPanelId panelId = sourceDock->addPanel("hierarchy", "Hierarchy", widget);
    ASSERT_NE(panelId, kInvalidDockPanelId);
    ASSERT_TRUE(sourceDock->setPanelIdentity(panelId, kLevelEditorRootId, "scene.yascene"));
    (void)sourceTree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_TRUE(sourceTree.contains(*widget));

    GUIWindowManager manager;
    ASSERT_TRUE(manager.init());
    EmptyDelegate content;
    FEditorNativeTearOff env{
        .coordinator = &manager,
        .content     = &content,
        .windows     = &windows,
        .spawners    = &spawners,
    };
    const FEditorTearOffResult result =
        tearOffEditorPanelToNativeWindow(env, source, *sourceDock, panelId);
    if (result.guiWindowId == 0) {
        GTEST_SKIP() << "SDL native window create failed";
    }

    EditorWindowSession* extra = windows.find(result.editorWindowId);
    ASSERT_NE(extra, nullptr);
    IGUIWindowSession* gui = manager.findSession(result.guiWindowId);
    ASSERT_NE(gui, nullptr);
    ASSERT_NE(gui->tree(), nullptr);
    EXPECT_EQ(extra->tree(), gui->tree());
    EXPECT_EQ(sourceDock->findPanel(panelId), nullptr);
    EXPECT_FALSE(sourceTree.contains(*widget));
    EXPECT_TRUE(gui->tree()->contains(*widget));
    EXPECT_EQ(widget->getTree(), gui->tree());

    const FDockContext::FPanel* moved =
        extra->surface().ownedNestedDock()->findPanel(result.targetPanelId);
    ASSERT_NE(moved, nullptr);
    EXPECT_EQ(moved->widget, widget);
    EXPECT_EQ(moved->ownerEditorId, kLevelEditorRootId);
    EXPECT_EQ(moved->documentKey, "scene.yascene");
    EXPECT_EQ(extra->root(kLevelEditorRootId).document(), doc);
    EXPECT_EQ(extra->surface().windowRootDock()->findPanelByStableKey("hierarchy"), nullptr);
}

TEST(EditorNativeTearOffTest, CloseDefaultWindowIsRejected)
{
    EditorWindowRegistry windows;
    FEditorNativeTearOff env{.windows = &windows};
    EXPECT_EQ(closeEditorWindow(env, kDefaultEditorWindowId), EEditorWindowCloseResult::RejectedLocked);
    EXPECT_EQ(windows.find(kDefaultEditorWindowId), &windows.defaultSession());
}

TEST(EditorNativeTearOffTest, OwnedToolRedocksHomeAndReclaimsEmptyWindow)
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

    EditorWindowRegistry windows;
    EditorWindowSession& source = windows.defaultSession();
    auto homeDock = source.surface().ownedNestedDockPtr();
    ASSERT_NE(homeDock, nullptr);
    homeDock->sourceScope = EDockSourceScope::EditorOwned;
    WidgetTree homeTree({.width = 400, .height = 300});
    attachDock(homeTree, homeDock);
    auto widget = std::make_shared<UICanvasPanel>("H");
    const DockPanelId panelId = homeDock->addPanel("hierarchy", "Hierarchy", widget);
    ASSERT_NE(panelId, kInvalidDockPanelId);
    ASSERT_TRUE(homeDock->setPanelIdentity(panelId, kLevelEditorRootId, "scene.yascene"));
    (void)homeTree.buildSnapshot(UIFrameBuildContext{});

    GUIWindowManager manager;
    ASSERT_TRUE(manager.init());
    EmptyDelegate content;
    FEditorNativeTearOff env{
        .coordinator = &manager,
        .content     = &content,
        .windows     = &windows,
        .spawners    = &spawners,
    };
    const FEditorTearOffResult torn =
        tearOffEditorPanelToNativeWindow(env, source, *homeDock, panelId);
    if (torn.guiWindowId == 0) {
        GTEST_SKIP() << "SDL native window create failed";
    }
    EditorWindowSession* extra = windows.find(torn.editorWindowId);
    ASSERT_NE(extra, nullptr);
    FDockContext* extraDock = extra->surface().ownedNestedDock();
    ASSERT_NE(extraDock, nullptr);
    EXPECT_FALSE(homeTree.contains(*widget));

    const FEditorRedockResult redock =
        redockEditorPanelToOwner(env, *extra, *extraDock, torn.targetPanelId);
    EXPECT_NE(redock.targetPanelId, kInvalidDockPanelId);
    EXPECT_TRUE(redock.bReclaimedSourceWindow);
    EXPECT_EQ(windows.find(torn.editorWindowId), nullptr);
    EXPECT_EQ(windows.extraCount(), 0u);
    EXPECT_EQ(manager.extraWindowCount(), 0u);
    EXPECT_TRUE(homeDock->hasPanel("hierarchy"));
    EXPECT_TRUE(homeTree.contains(*widget));
    EXPECT_EQ(widget->getTree(), &homeTree);
    const FDockContext::FPanel* homePanel = homeDock->findPanel(redock.targetPanelId);
    ASSERT_NE(homePanel, nullptr);
    EXPECT_EQ(homePanel->ownerEditorId, kLevelEditorRootId);
    EXPECT_EQ(homePanel->documentKey, "scene.yascene");
}

TEST(EditorNativeTearOffTest, CloseExtraWindowRedocksRootEditorHome)
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
    EditorWindowSession& source = windows.defaultSession();
    auto homeDock = source.surface().windowRootDockPtr();
    ASSERT_NE(homeDock, nullptr);
    homeDock->sourceScope = EDockSourceScope::WindowRoot;
    WidgetTree homeTree({.width = 400, .height = 300});
    attachDock(homeTree, homeDock);
    auto widget = std::make_shared<UICanvasPanel>("M");
    const DockPanelId panelId = homeDock->addPanel("material-editor", "Material", widget);
    ASSERT_NE(panelId, kInvalidDockPanelId);
    ASSERT_TRUE(homeDock->setPanelIdentity(panelId, kMaterialEditorRootId, "M.mat"));
    (void)homeTree.buildSnapshot(UIFrameBuildContext{});

    GUIWindowManager manager;
    ASSERT_TRUE(manager.init());
    EmptyDelegate content;
    FEditorNativeTearOff env{
        .coordinator = &manager,
        .content     = &content,
        .windows     = &windows,
        .spawners    = &spawners,
    };
    const FEditorTearOffResult torn =
        tearOffEditorPanelToNativeWindow(env, source, *homeDock, panelId);
    if (torn.guiWindowId == 0) {
        GTEST_SKIP() << "SDL native window create failed";
    }
    EXPECT_EQ(windows.extraCount(), 1u);

    EXPECT_EQ(closeEditorWindow(env, torn.editorWindowId), EEditorWindowCloseResult::Reclaimed);
    EXPECT_EQ(windows.find(torn.editorWindowId), nullptr);
    EXPECT_EQ(windows.extraCount(), 0u);
    EXPECT_EQ(manager.extraWindowCount(), 0u);
    EXPECT_TRUE(homeDock->hasPanel("material-editor"));
    EXPECT_TRUE(homeTree.contains(*widget));
    const FDockContext::FPanel* homePanel = homeDock->findPanelByStableKey("material-editor");
    ASSERT_NE(homePanel, nullptr);
    EXPECT_EQ(homePanel->ownerEditorId, kMaterialEditorRootId);
    EXPECT_EQ(homePanel->documentKey, "M.mat");
}

TEST(EditorNativeTearOffTest, LockedLevelCannotRedockAwayFromDefault)
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

    EditorWindowRegistry windows;
    EditorWindowSession* extra = windows.create(2);
    ASSERT_NE(extra, nullptr);
    FDockContext* home = windows.defaultSession().surface().windowRootDock();
    FDockContext* other = extra->surface().windowRootDock();
    ASSERT_NE(home, nullptr);
    ASSERT_NE(other, nullptr);
    auto widget = std::make_shared<UICanvasPanel>("L");
    const DockPanelId panelId = home->addPanel("level-editor", "Level", widget);
    ASSERT_NE(panelId, kInvalidDockPanelId);
    ASSERT_TRUE(home->setPanelIdentity(panelId, kLevelEditorRootId, "scene.yascene"));

    FEditorNativeTearOff env{.windows = &windows, .spawners = &spawners};
    const FEditorRedockResult redock = redockEditorPanelToOwner(env, windows.defaultSession(), *home, panelId);
    EXPECT_EQ(redock.targetPanelId, kInvalidDockPanelId);
    EXPECT_FALSE(redock.bReclaimedSourceWindow);
    ASSERT_NE(home->findPanel(panelId), nullptr);
    EXPECT_EQ(home->findPanel(panelId)->widget, widget);
    EXPECT_EQ(other->findPanelByStableKey("level-editor"), nullptr);
    EXPECT_EQ(windows.extraCount(), 1u);
}

TEST(EditorNativeTearOffTest, ExtraTickTreesResizeCloseSoakMatchesProductPath)
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
    EditorWindowSession& source = windows.defaultSession();
    auto homeDock = source.surface().windowRootDockPtr();
    ASSERT_NE(homeDock, nullptr);
    homeDock->sourceScope = EDockSourceScope::WindowRoot;
    WidgetTree homeTree({.width = 400, .height = 300});
    attachDock(homeTree, homeDock);
    auto widget = std::make_shared<UICanvasPanel>("M");
    const DockPanelId panelId = homeDock->addPanel("material-editor", "Material", widget);
    ASSERT_NE(panelId, kInvalidDockPanelId);
    ASSERT_TRUE(homeDock->setPanelIdentity(panelId, kMaterialEditorRootId, "M.mat"));
    (void)homeTree.buildSnapshot(UIFrameBuildContext{});

    GUIWindowManager manager;
    ASSERT_TRUE(manager.init());
    EmptyDelegate content;
    FEditorNativeTearOff env{
        .coordinator = &manager,
        .content     = &content,
        .windows     = &windows,
        .spawners    = &spawners,
    };
    const FEditorTearOffResult torn =
        tearOffEditorPanelToNativeWindow(env, source, *homeDock, panelId);
    if (torn.guiWindowId == 0) {
        GTEST_SKIP() << "SDL native window create failed";
    }

    EditorWindowSession* extra = windows.find(torn.editorWindowId);
    ASSERT_NE(extra, nullptr);
    WidgetTree* extraTree = extra->tree();
    ASSERT_NE(extraTree, nullptr);
    EXPECT_TRUE(extraTree->contains(*widget));
    EXPECT_FALSE(homeTree.contains(*widget));

    bool bClosed = false;
    constexpr int kFrames = 48;
    for (int frame = 0; frame < kFrames; ++frame) {
        if (!bClosed && frame == 12) {
            ASSERT_TRUE(manager.dispatchEvent(WindowResizeEvent(torn.guiWindowId, 360, 280)));
            EXPECT_EQ(extraTree->getLogicalExtent().width, 360u);
            EXPECT_EQ(homeTree.getLogicalExtent().width, 400u);
        }
        if (!bClosed && frame == 20) {
            ASSERT_TRUE(manager.dispatchEvent(WindowMinimizeEvent(torn.guiWindowId)));
        }
        if (!bClosed && frame == 24) {
            ASSERT_TRUE(manager.dispatchEvent(WindowRestoreEvent(torn.guiWindowId)));
        }
        if (!bClosed && frame == 40) {
            ASSERT_TRUE(manager.dispatchEvent(WindowCloseEvent(torn.guiWindowId)));
        }

        IGUIWindowSession* gui = manager.findSession(torn.guiWindowId);
        if (gui && gui->closeRequested()) {
            EXPECT_EQ(closeEditorWindow(env, torn.editorWindowId), EEditorWindowCloseResult::Reclaimed);
            bClosed = true;
            extraTree = nullptr;
            extra = nullptr;
        }
        else {
            manager.tickTrees(0.016f);
            manager.renderAll();
        }

        EXPECT_EQ(windows.find(kDefaultEditorWindowId), &source);
        if (!bClosed) {
            EXPECT_TRUE(extraTree->contains(*widget));
            EXPECT_FALSE(homeTree.contains(*widget));
            EXPECT_EQ(widget->getTree(), extraTree);
            EXPECT_EQ(windows.extraCount(), 1u);
        }
    }

    EXPECT_TRUE(bClosed);
    EXPECT_EQ(windows.find(torn.editorWindowId), nullptr);
    EXPECT_EQ(windows.extraCount(), 0u);
    EXPECT_EQ(manager.extraWindowCount(), 0u);
    EXPECT_TRUE(homeDock->hasPanel("material-editor"));
    EXPECT_TRUE(homeTree.contains(*widget));
    EXPECT_EQ(widget->getTree(), &homeTree);
}

TEST(EditorNativeTearOffTest, DockSpaceNoTargetCreatesNativeWindowWithoutOverlay)
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
    EditorWindowSession& source = windows.defaultSession();
    auto sourceDock = source.surface().windowRootDockPtr();
    ASSERT_NE(sourceDock, nullptr);
    sourceDock->bAllowFloating = true;
    sourceDock->bAllowTearOff = true;
    sourceDock->sourceScope = EDockSourceScope::WindowRoot;
    sourceDock->hostWindowId = source.windowId();

    WidgetTree sourceTree({.width = 1000, .height = 700});
    auto spaceWidget = std::make_shared<UIDockSpace>("SourceDock");
    FCanvasSlotArgs dockArgs;
    dockArgs.offset = {0.0f, 0.0f};
    dockArgs.fixedSize = {400.0f, 300.0f};
    (void)sourceTree.attach(*sourceTree.getLayer(WidgetTree::ELayer::Content), spaceWidget, dockArgs);
    spaceWidget->setContext(sourceDock);
    auto widget = std::make_shared<UICanvasPanel>("M");
    const DockPanelId panelId = sourceDock->addPanel("material-editor", "Material", widget);
    ASSERT_NE(panelId, kInvalidDockPanelId);
    ASSERT_TRUE(sourceDock->setPanelIdentity(panelId, kMaterialEditorRootId, "M.mat"));
    (void)sourceTree.buildSnapshot(UIFrameBuildContext{});

    GUIWindowManager manager;
    ASSERT_TRUE(manager.init());
    EmptyDelegate content;
    FEditorNativeTearOff env{
        .coordinator = &manager,
        .content     = &content,
        .windows     = &windows,
        .spawners    = &spawners,
    };
    source.surface().setOnDockNoTargetTearOff(
        [&](FDockContext& dock, uint64_t id, const glm::vec2& pos, const glm::vec2& size) {
            return handleDockNoTargetTearOff(env, source, dock, id, pos, size);
        });

    UIDockSpace* space = findDescendantOfType<UIDockSpace>(
        *sourceTree.getLayer(WidgetTree::ELayer::Content));
    ASSERT_NE(space, nullptr);
    UITabBar* tabBar = findDescendantOfType<UITabBar>(*space);
    ASSERT_NE(tabBar, nullptr);
    ASSERT_TRUE(static_cast<bool>(tabBar->_onTabDragBegin));

    tabBar->_onTabDragBegin(0, "Material");
    ASSERT_TRUE(sourceTree.isDragging());
    sourceTree.endDrag({800.0f, 500.0f});

    if (windows.extraCount() == 0) {
        GTEST_SKIP() << "SDL native window create failed";
    }

    EXPECT_EQ(windows.extraCount(), 1u);
    EXPECT_EQ(sourceDock->findPanel(panelId), nullptr);
    EXPECT_FALSE(sourceDock->isPanelFloating(panelId));
    EXPECT_FALSE(sourceTree.contains(*widget));
    EditorWindowSession* extra = nullptr;
    windows.forEach([&](EditorWindowSession& session) {
        if (session.windowId() != kDefaultEditorWindowId) {
            extra = &session;
        }
    });
    ASSERT_NE(extra, nullptr);
    EXPECT_TRUE(extra->surface().windowRootDock()->hasPanel("material-editor"));
    EXPECT_NE(extra->tree(), nullptr);
    EXPECT_TRUE(extra->tree()->contains(*widget));
}

TEST(EditorNativeTearOffTest, DockSpaceNoTargetLockedStaysPutWithoutOverlay)
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

    EditorWindowRegistry windows;
    EditorWindowSession& source = windows.defaultSession();
    auto sourceDock = source.surface().windowRootDockPtr();
    ASSERT_NE(sourceDock, nullptr);
    sourceDock->bAllowFloating = true;
    sourceDock->bAllowTearOff = true;

    WidgetTree sourceTree({.width = 1000, .height = 700});
    auto spaceWidget = std::make_shared<UIDockSpace>("SourceDock");
    FCanvasSlotArgs dockArgs;
    dockArgs.offset = {0.0f, 0.0f};
    dockArgs.fixedSize = {400.0f, 300.0f};
    (void)sourceTree.attach(*sourceTree.getLayer(WidgetTree::ELayer::Content), spaceWidget, dockArgs);
    spaceWidget->setContext(sourceDock);
    auto widget = std::make_shared<UICanvasPanel>("L");
    const DockPanelId panelId = sourceDock->addPanel("level-editor", "Level", widget);
    ASSERT_NE(panelId, kInvalidDockPanelId);
    ASSERT_TRUE(sourceDock->setPanelIdentity(panelId, kLevelEditorRootId, "scene.yascene"));
    (void)sourceTree.buildSnapshot(UIFrameBuildContext{});

    GUIWindowManager manager;
    ASSERT_TRUE(manager.init());
    EmptyDelegate content;
    FEditorNativeTearOff env{
        .coordinator = &manager,
        .content     = &content,
        .windows     = &windows,
        .spawners    = &spawners,
    };
    source.surface().setOnDockNoTargetTearOff(
        [&](FDockContext& dock, uint64_t id, const glm::vec2& pos, const glm::vec2& size) {
            return handleDockNoTargetTearOff(env, source, dock, id, pos, size);
        });

    UIDockSpace* space = findDescendantOfType<UIDockSpace>(
        *sourceTree.getLayer(WidgetTree::ELayer::Content));
    ASSERT_NE(space, nullptr);
    UITabBar* tabBar = findDescendantOfType<UITabBar>(*space);
    ASSERT_NE(tabBar, nullptr);

    tabBar->_onTabDragBegin(0, "Level");
    sourceTree.endDrag({800.0f, 500.0f});

    EXPECT_EQ(windows.extraCount(), 0u);
    EXPECT_EQ(manager.extraWindowCount(), 0u);
    ASSERT_NE(sourceDock->findPanel(panelId), nullptr);
    EXPECT_EQ(sourceDock->findPanel(panelId)->widget, widget);
    EXPECT_FALSE(sourceDock->isPanelFloating(panelId));
    EXPECT_TRUE(sourceTree.contains(*widget));
}

TEST(EditorNativeTearOffTest, UniqueExtraTabTearOffReclaimsEmptySource)
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
    EditorWindowSession& source = windows.defaultSession();
    auto sourceDock = source.surface().windowRootDockPtr();
    ASSERT_NE(sourceDock, nullptr);
    sourceDock->bAllowFloating = true;
    sourceDock->bAllowTearOff = true;
    auto widget = std::make_shared<UICanvasPanel>("M");
    const DockPanelId panelId = sourceDock->addPanel("material-editor", "Material", widget);
    ASSERT_NE(panelId, kInvalidDockPanelId);
    ASSERT_TRUE(sourceDock->setPanelIdentity(panelId, kMaterialEditorRootId, "M.mat"));

    GUIWindowManager manager;
    ASSERT_TRUE(manager.init());
    EmptyDelegate content;
    FEditorNativeTearOff env{
        .coordinator = &manager,
        .content     = &content,
        .windows     = &windows,
        .spawners    = &spawners,
    };
    const FEditorTearOffResult first =
        tearOffEditorPanelToNativeWindow(env, source, *sourceDock, panelId);
    if (first.guiWindowId == 0) {
        GTEST_SKIP() << "SDL native window create failed";
    }
    EXPECT_EQ(windows.extraCount(), 1u);
    EditorWindowSession* extra = windows.find(first.editorWindowId);
    ASSERT_NE(extra, nullptr);
    FDockContext* extraDock = extra->surface().windowRootDock();
    ASSERT_NE(extraDock, nullptr);
    const FDockContext::FPanel* moved = extraDock->findPanelByStableKey("material-editor");
    ASSERT_NE(moved, nullptr);

    const FEditorTearOffResult second =
        tearOffEditorPanelToNativeWindow(env, *extra, *extraDock, moved->id);
    if (second.guiWindowId == 0) {
        GTEST_SKIP() << "SDL native window create failed";
    }
    EXPECT_EQ(windows.extraCount(), 1u);
    EXPECT_EQ(windows.find(first.editorWindowId), nullptr);
    EditorWindowSession* kept = windows.find(second.editorWindowId);
    ASSERT_NE(kept, nullptr);
    EXPECT_TRUE(kept->surface().windowRootDock()->hasPanel("material-editor"));
}

} // namespace ya
