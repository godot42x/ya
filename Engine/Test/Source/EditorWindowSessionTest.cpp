#include "GameEditor/UI/EditorWindowRegistry.h"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iterator>
#include <string>

namespace ya
{

namespace
{

std::string readEngineSource(const std::filesystem::path& relative)
{
    const auto path = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / relative;
    std::ifstream in(path);
    EXPECT_TRUE(in.good()) << "missing " << path.string();
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace

TEST(EditorWindowSessionTest, RegistryFindsOnlyTheDefaultWindow)
{
    EditorWindowRegistry windows;
    EditorWindowSession* found = windows.find(kDefaultEditorWindowId);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found, &windows.defaultSession());
    EXPECT_EQ(found->windowId(), kDefaultEditorWindowId);
    EXPECT_EQ(windows.find(kInvalidEditorWindowId), nullptr);
    EXPECT_EQ(windows.find(kDefaultEditorWindowId + 1), nullptr);
}

TEST(EditorWindowSessionTest, RegistryCreatesIsolatedSecondSession)
{
    EditorWindowRegistry windows;
    EXPECT_EQ(windows.create(kInvalidEditorWindowId), nullptr);
    EXPECT_EQ(windows.create(kDefaultEditorWindowId), nullptr);

    constexpr EditorWindowId kExtra = 2;
    EditorWindowSession* extra = windows.create(kExtra);
    ASSERT_NE(extra, nullptr);
    EXPECT_EQ(extra->windowId(), kExtra);
    EXPECT_EQ(windows.find(kExtra), extra);
    EXPECT_NE(extra, &windows.defaultSession());
    EXPECT_EQ(windows.create(kExtra), nullptr);

    windows.defaultSession().activeRoot().selection().select("entity-a");
    extra->activeRoot().selection().select("entity-b");
    EXPECT_TRUE(windows.defaultSession().activeRoot().selection().contains("entity-a"));
    EXPECT_FALSE(windows.defaultSession().activeRoot().selection().contains("entity-b"));
    EXPECT_TRUE(extra->activeRoot().selection().contains("entity-b"));
    EXPECT_FALSE(extra->activeRoot().selection().contains("entity-a"));

    EXPECT_NE(&windows.defaultSession().surface(), &extra->surface());
    EXPECT_NE(windows.defaultSession().surface().windowRootDock(), extra->surface().windowRootDock());
    EXPECT_NE(windows.defaultSession().surface().ownedNestedDock(), extra->surface().ownedNestedDock());
    EXPECT_NE(&windows.defaultSession().surface().viewportOverlayHost(),
              &extra->surface().viewportOverlayHost());
    EXPECT_EQ(windows.defaultSession().tree(), nullptr);
    EXPECT_EQ(extra->tree(), nullptr);

    EXPECT_FALSE(windows.destroy(kDefaultEditorWindowId));
    EXPECT_TRUE(windows.destroy(kExtra));
    EXPECT_EQ(windows.find(kExtra), nullptr);
    EXPECT_FALSE(windows.destroy(kExtra));
}

TEST(EditorWindowSessionTest, SessionOwnsSurfaceWithoutBuildingChrome)
{
    const EditorWindowSession session;
    EXPECT_EQ(session.windowId(), kDefaultEditorWindowId);
    EXPECT_EQ(session.tree(), nullptr);
    ASSERT_NE(session.surface().windowRootDock(), nullptr);
    ASSERT_NE(session.surface().ownedNestedDock(), nullptr);
    EXPECT_NE(session.surface().windowRootDock(), session.surface().ownedNestedDock());
    EXPECT_FALSE(session.wantsTextInput());
    EXPECT_FALSE(session.isViewportHovered());
}

TEST(EditorWindowSessionTest, ChromeStacksPageTabsThenMenu)
{
    const std::string surfaceCpp =
        readEngineSource("Source/Applications/GameEditor/UI/EditorSurface.cpp");
    EXPECT_NE(surfaceCpp.find("EditorPageTabs"), std::string::npos);
    EXPECT_NE(surfaceCpp.find("syncPageTabs"), std::string::npos);
    EXPECT_NE(surfaceCpp.find("WindowPageTab"), std::string::npos);
}

TEST(EditorWindowSessionTest, WindowRootHostDeclaresWindowRootDockScope)
{
    const std::string surfaceCpp =
        readEngineSource("Source/Applications/GameEditor/UI/EditorSurface.cpp");
    EXPECT_NE(surfaceCpp.find("targetPlacement = EEditorTabPlacement::WindowRootDock"),
              std::string::npos);
    EXPECT_NE(surfaceCpp.find("targetPlacement = EEditorTabPlacement::EditorOwnedNested"),
              std::string::npos);
    EXPECT_NE(surfaceCpp.find("nestedWorkspace = &_ownedWorkspace"), std::string::npos);
    EXPECT_NE(surfaceCpp.find("presentSurface  = context.presentSurface"), std::string::npos);
}

TEST(EditorWindowSessionTest, DockContextDoesNotKnowEditorRoots)
{
    const std::string dockH =
        readEngineSource("Source/Framework/GUI/Runtime/Widgets/Controls/DockSpace/DockContext.h");
    const std::string dockCpp =
        readEngineSource("Source/Framework/GUI/Runtime/Widgets/Controls/DockSpace/DockContext.cpp");
    for (const std::string* text : {&dockH, &dockCpp}) {
        EXPECT_EQ(text->find("EditorRootId"), std::string::npos);
        EXPECT_EQ(text->find("EEditorTabScope"), std::string::npos);
        EXPECT_EQ(text->find("EEditorTabPlacement"), std::string::npos);
        EXPECT_EQ(text->find("GameEditor"), std::string::npos);
    }
    EXPECT_NE(dockH.find("FDockFloatingPlacement"), std::string::npos);
    EXPECT_NE(dockH.find("InProcessOverlay"), std::string::npos);
    EXPECT_NE(dockH.find("NativeWindow"), std::string::npos);
    EXPECT_EQ(dockH.find("GUIWindowManager"), std::string::npos);
    EXPECT_EQ(dockH.find("NativeWindowManager"), std::string::npos);
    EXPECT_EQ(dockH.find("IGUIWindowCoordinator"), std::string::npos);
    EXPECT_EQ(dockCpp.find("GUIWindowManager"), std::string::npos);
    EXPECT_EQ(dockCpp.find("createSession"), std::string::npos);

    const std::string windowSessionH =
        readEngineSource("Source/Framework/GUI/Host/Window/GUIWindowSession.h");
    const std::string windowManagerH =
        readEngineSource("Source/Framework/GUI/Host/Window/GUIWindowManager.h");
    EXPECT_EQ(windowSessionH.find("DockContext.h"), std::string::npos);
    EXPECT_EQ(windowSessionH.find("FDockContext"), std::string::npos);
    EXPECT_EQ(windowSessionH.find("FDockFloatingWindowId"), std::string::npos);
    EXPECT_EQ(windowManagerH.find("FDockContext"), std::string::npos);
    EXPECT_EQ(windowManagerH.find("realizeNativeDockPlacement"), std::string::npos);

    const std::string editorThemeH =
        readEngineSource("Source/Applications/GameEditor/include/GameEditor/UI/EditorTheme.h");
    EXPECT_EQ(editorThemeH.find("WorkbenchTheme.h"), std::string::npos);
    EXPECT_NE(editorThemeH.find("DefaultChromeTheme.h"), std::string::npos);

    const std::string workspaceH =
        readEngineSource("Source/Applications/GameEditor/include/GameEditor/UI/EditorDockWorkspace.h");
    const std::string workspaceCpp =
        readEngineSource("Source/Applications/GameEditor/UI/EditorDockWorkspace.cpp");
    for (const std::string* text : {&workspaceH, &workspaceCpp}) {
        EXPECT_EQ(text->find("GUIWindowManager"), std::string::npos);
        EXPECT_EQ(text->find("IGUIWindowCoordinator"), std::string::npos);
        EXPECT_EQ(text->find("createSession"), std::string::npos);
    }

    const std::string tearOffH =
        readEngineSource("Source/Applications/GameEditor/include/GameEditor/UI/EditorNativeTearOff.h");
    const std::string tearOffCpp =
        readEngineSource("Source/Applications/GameEditor/UI/EditorNativeTearOff.cpp");
    EXPECT_NE(tearOffH.find("IGUIWindowCoordinator"), std::string::npos);
    EXPECT_NE(tearOffCpp.find("realizeNativeDockPlacement"), std::string::npos);
    EXPECT_NE(tearOffCpp.find("transferNativePlacementTo"), std::string::npos);
}

TEST(EditorWindowSessionTest, TickAppForwardingIsGone)
{
    const std::string surfaceH =
        readEngineSource("Source/Applications/GameEditor/include/GameEditor/UI/EditorSurface.h");
    const std::string surfaceCpp =
        readEngineSource("Source/Applications/GameEditor/UI/EditorSurface.cpp");
    const std::string sessionH =
        readEngineSource("Source/Applications/GameEditor/include/GameEditor/UI/EditorWindowSession.h");
    const std::string sessionCpp =
        readEngineSource("Source/Applications/GameEditor/UI/EditorWindowSession.cpp");
    for (const std::string* text : {&surfaceH, &surfaceCpp, &sessionH, &sessionCpp}) {
        EXPECT_EQ(text->find("tick(App"), std::string::npos);
    }
}

TEST(EditorWindowSessionTest, HostAndSurfaceDoNotReadPrimarySwapchain)
{
    const char* files[] = {
        "Source/Applications/GameEditor/include/GameEditor/UI/EditorSurface.h",
        "Source/Applications/GameEditor/UI/EditorSurface.cpp",
        "Source/Applications/GameEditor/include/GameEditor/UI/EditorWindowSession.h",
        "Source/Applications/GameEditor/UI/EditorWindowSession.cpp",
        "Source/Applications/GameEditor/include/GameEditor/UI/EditorSurfaceContext.h",
        "Source/Applications/GameEditor/UI/EditorSurfaceContext.cpp",
        "Source/Applications/GameEditor/EditorModule.cpp",
    };
    for (const char* relative : files) {
        const std::string text = readEngineSource(relative);
        EXPECT_EQ(text.find("primarySwapchain"), std::string::npos) << relative;
        EXPECT_EQ(text.find("primaryWindow"), std::string::npos) << relative;
    }
}

TEST(EditorWindowSessionTest, InputRoutesByWindowId)
{
    const std::string inputH =
        readEngineSource("Source/Applications/GameEditor/Input/EditorInputNode.h");
    EXPECT_NE(inputH.find("EditorWindowRegistry& windows"), std::string::npos);
    EXPECT_NE(inputH.find("_windowId"), std::string::npos);
    EXPECT_NE(inputH.find("GUIWindowManager* extraWindows"), std::string::npos);
    EXPECT_EQ(inputH.find("EditorWindowSession* _session"), std::string::npos);

    const std::string inputCpp =
        readEngineSource("Source/Applications/GameEditor/Input/EditorInputNode.cpp");
    EXPECT_NE(inputCpp.find("_windows->find(_windowId)"), std::string::npos);
    EXPECT_NE(inputCpp.find("_extraWindows->dispatchEvent(event)"), std::string::npos);
    EXPECT_NE(inputCpp.find("_dragRouter->route(event)"), std::string::npos);
    EXPECT_NE(inputCpp.find("_dragRouter->cursor()"), std::string::npos);
    EXPECT_NE(inputCpp.find("context.router.getWindow()"), std::string::npos);
    EXPECT_NE(inputCpp.find("EInputCancelReason::ModuleDetached"), std::string::npos);
    EXPECT_EQ(inputCpp.find("getNativeWindowManager"), std::string::npos);
    EXPECT_EQ(inputCpp.find("_session"), std::string::npos);

    const std::string moduleCpp =
        readEngineSource("Source/Applications/GameEditor/EditorModule.cpp");
    EXPECT_NE(moduleCpp.find("_inputNode.bind(app, *_layer, _windows, window->windowId(), &_guiWindows, &_dragRouter)"),
              std::string::npos);
    EXPECT_NE(moduleCpp.find("GUIDragRouter"), std::string::npos);
    EXPECT_NE(inputH.find("GUIDragRouter* dragRouter"), std::string::npos);
    EXPECT_NE(moduleCpp.find("restoreEditorExtraWindows"), std::string::npos);
    EXPECT_NE(moduleCpp.find("onAfterPresent"), std::string::npos);
    EXPECT_NE(moduleCpp.find("persistEditorWindowLayout(_windows, mainNativeWindow(), &_guiWindows)"),
              std::string::npos);
    EXPECT_NE(moduleCpp.find("_guiWindows.renderAll()"), std::string::npos);
    EXPECT_NE(moduleCpp.find("_guiWindows.tickTrees(dt)"), std::string::npos);
    EXPECT_NE(moduleCpp.find("_guiWindows.setFocusedWindow(0)"), std::string::npos);
    EXPECT_EQ(moduleCpp.find("_guiWindows.tickAll("), std::string::npos);
    EXPECT_EQ(moduleCpp.find("IRender::create"), std::string::npos);
    EXPECT_EQ(moduleCpp.find("SDL_CreateWindow"), std::string::npos);
    EXPECT_NE(moduleCpp.find("setOnDockNoTargetTearOff"), std::string::npos);
    EXPECT_NE(moduleCpp.find("handleDockNoTargetTearOff"), std::string::npos);
    EXPECT_NE(moduleCpp.find("hookNativeTearOff"), std::string::npos);
    EXPECT_NE(moduleCpp.find("hookExtraPersist(*extra)"), std::string::npos);

    const std::string managerCpp =
        readEngineSource("Source/Framework/GUI/Host/Window/GUIWindowManager.cpp");
    EXPECT_NE(managerCpp.find("bindSdlClipboard(*session->ownedTree)"), std::string::npos);
    EXPECT_EQ(managerCpp.find("Tree-local in-memory clipboard"), std::string::npos);

    const std::string surfaceH =
        readEngineSource("Source/Applications/GameEditor/include/GameEditor/UI/EditorSurface.h");
    const std::string surfaceCpp =
        readEngineSource("Source/Applications/GameEditor/UI/EditorSurface.cpp");
    EXPECT_EQ(surfaceH.find("IGUIWindowCoordinator"), std::string::npos);
    EXPECT_EQ(surfaceCpp.find("IGUIWindowCoordinator"), std::string::npos);
    EXPECT_NE(surfaceH.find("setOnDockNoTargetTearOff"), std::string::npos);

    const std::string dockSpaceCpp =
        readEngineSource("Source/Framework/GUI/Runtime/Widgets/Controls/DockSpace/DockSpace.cpp");
    EXPECT_NE(dockSpaceCpp.find("realizeNoTargetTearOff"), std::string::npos);

    const std::string orchestrator =
        readEngineSource("Source/Applications/GameRuntime/Lifecycle/GameRuntimeFrameOrchestrator.cpp");
    EXPECT_NE(orchestrator.find("presentModuleExtras"), std::string::npos);
    EXPECT_NE(orchestrator.find("app.presentModuleExtras(dt)"), std::string::npos);

    const std::string eventRouter =
        readEngineSource("Source/Applications/GameRuntime/Lifecycle/AppEventRouter.cpp");
    EXPECT_NE(eventRouter.find(
                  "mainWindowID != 0 && event.getWindowID() != 0 && event.getWindowID() != mainWindowID"),
              std::string::npos);
}

TEST(EditorWindowSessionTest, ImeUsesTreeCapabilityNotInspectorCast)
{
    const std::string surfaceCpp =
        readEngineSource("Source/Applications/GameEditor/UI/EditorSurface.cpp");
    EXPECT_NE(surfaceCpp.find("_tree->wantsTextInput()"), std::string::npos);
    EXPECT_EQ(surfaceCpp.find("dynamic_cast<EditorInspectorTab"), std::string::npos);
    EXPECT_EQ(surfaceCpp.find("EditorInspectorTab"), std::string::npos);

    const std::string inputCpp =
        readEngineSource("Source/Applications/GameEditor/Input/EditorInputNode.cpp");
    EXPECT_NE(inputCpp.find("session->wantsTextInput()"), std::string::npos);
    EXPECT_EQ(inputCpp.find("dynamic_cast<EditorInspectorTab"), std::string::npos);
    EXPECT_EQ(inputCpp.find("EditorInspectorTab"), std::string::npos);

    const std::string inspectorH =
        readEngineSource("Source/Applications/GameEditor/include/GameEditor/UI/EditorInspectorTab.h");
    EXPECT_EQ(inspectorH.find("wantsTextInput"), std::string::npos);

    const std::string sectionH =
        readEngineSource("Source/Applications/GameEditor/include/GameEditor/UI/EditorAutoPropertySection.h");
    EXPECT_EQ(sectionH.find("wantsTextInput"), std::string::npos);
}

TEST(EditorWindowSessionTest, SessionOwnedTabsDoNotUseAppGetOrPrimarySwapchain)
{
    const char* files[] = {
        "Source/Applications/GameEditor/UI/EditorContentBrowserTab.cpp",
        "Source/Applications/GameEditor/UI/EditorRuntimeToolsTab.cpp",
        "Source/Applications/GameEditor/UI/EditorRenderSettingsTab.cpp",
        "Source/Applications/GameEditor/UI/EditorPlayToolbarTab.cpp",
        "Source/Applications/GameEditor/UI/EditorFontAtlasTab.cpp",
        "Source/Applications/GameEditor/UI/RuntimeRenderSettingsSection.cpp",
        "Source/Applications/GameEditor/UI/EditorInspectorTab.cpp",
        "Source/Applications/GameEditor/UI/EditorViewportTab.cpp",
        "Source/Applications/GameEditor/UI/EditorHierarchyTab.cpp",
        "Source/Applications/GameEditor/UI/EditorUIDesignerTab.cpp",
        "Source/Applications/GameEditor/UI/EditorUIDesignerTools.cpp",
        "Source/Applications/GameEditor/UI/EditorDocumentEditorTab.cpp",
        "Source/Applications/GameEditor/UI/EditorNestedDockHost.cpp",
    };
    for (const char* relative : files) {
        const std::string text = readEngineSource(relative);
        EXPECT_EQ(text.find("App::get()"), std::string::npos) << relative;
        EXPECT_EQ(text.find("primarySwapchain"), std::string::npos) << relative;
    }

    const std::string settings = readEngineSource(
        "Source/Applications/GameEditor/UI/RuntimeRenderSettingsSection.cpp");
    EXPECT_EQ(settings.find("LightStage.h"), std::string::npos);
    EXPECT_EQ(settings.find("SSAOStage.h"), std::string::npos);
    EXPECT_EQ(settings.find("getState()"), std::string::npos);
}

} // namespace ya
