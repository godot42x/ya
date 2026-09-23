#include "GUI/Host/GUIWindowChrome.h"
#include "GUI/Host/GUIWindowManager.h"
#include "GUI/Host/GUIWindowPlacement.h"
#include "GUI/Widgets/WidgetTree.h"
#include "RHI/NativeWindow.h"

#include <glm/glm.hpp>

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

void expectNoPlatformChrome(const std::string& text, const char* file)
{
    EXPECT_EQ(text.find("NSWindow"), std::string::npos) << file;
    EXPECT_EQ(text.find("AppKit"), std::string::npos) << file;
    EXPECT_EQ(text.find("titlebarAppearsTransparent"), std::string::npos) << file;
    EXPECT_EQ(text.find("winuser.h"), std::string::npos) << file;
    EXPECT_EQ(text.find("DwmExtendFrame"), std::string::npos) << file;
    EXPECT_EQ(text.find("WM_NCHITTEST"), std::string::npos) << file;
    EXPECT_EQ(text.find("HWND"), std::string::npos) << file;
}

struct NamedWindowDelegate final : IGUIAppDelegate
{
    void buildUI(WidgetTree&) override {}
};

} // namespace

TEST(GUIWindowChromeTest, PlatformDefaultAndCapabilities)
{
    const FWindowChromeCapabilities caps = queryWindowChromeCapabilities();
    EXPECT_TRUE(caps.nativeDecorations);
    EXPECT_TRUE(caps.clientDrawn);
    EXPECT_TRUE(caps.fullscreenMaximize);
    EXPECT_TRUE(caps.accessibility);
#if defined(__APPLE__)
    EXPECT_EQ(defaultWindowChromeMode(), EWindowChromeMode::Hybrid);
    EXPECT_TRUE(caps.hybridTitleContent);
    EXPECT_TRUE(caps.systemButtons);
    EXPECT_EQ(resolveWindowChromeMode(EWindowChromeMode::Hybrid), EWindowChromeMode::Hybrid);
    EXPECT_EQ(resolveWindowChromeMode(EWindowChromeMode::Native), EWindowChromeMode::Native);
    EXPECT_EQ(resolveWindowChromeMode(EWindowChromeMode::ClientDrawn), EWindowChromeMode::ClientDrawn);
#else
    EXPECT_EQ(defaultWindowChromeMode(), EWindowChromeMode::Native);
    EXPECT_FALSE(caps.hybridTitleContent);
    EXPECT_EQ(resolveWindowChromeMode(EWindowChromeMode::Hybrid), EWindowChromeMode::Native);
    EXPECT_EQ(resolveWindowChromeMode(EWindowChromeMode::Native), EWindowChromeMode::Native);
    EXPECT_EQ(resolveWindowChromeMode(EWindowChromeMode::ClientDrawn), EWindowChromeMode::ClientDrawn);
#endif
}

TEST(GUIWindowChromeTest, HybridLayoutReservesTrafficLightsAndDrag)
{
    const FWindowChromeCapabilities caps = queryWindowChromeCapabilities();
    const FWindowChromeInsets safe{.left = 78.0f, .top = 28.0f, .right = 0.0f, .bottom = 0.0f};
    const FWindowChromeLayout layout =
        makeWindowChromeLayout(EWindowChromeMode::Hybrid, caps, Extent2D{800, 600}, safe, true);

    EXPECT_EQ(layout.mode, EWindowChromeMode::Hybrid);
    EXPECT_FLOAT_EQ(layout.contentInsets.top, 28.0f);
    EXPECT_FLOAT_EQ(layout.contentInsets.left, 78.0f);
    EXPECT_FLOAT_EQ(layout.systemButtons.width, 78.0f);
    EXPECT_TRUE(layout.systemButtons.contains(10.0f, 10.0f));
    EXPECT_EQ(classifyWindowChromeHit(layout, 10.0f, 10.0f), EWindowChromeHit::SystemButton);
    EXPECT_EQ(classifyWindowChromeHit(layout, 200.0f, 10.0f), EWindowChromeHit::Drag);
    EXPECT_EQ(classifyWindowChromeHit(layout, 750.0f, 10.0f), EWindowChromeHit::Drag);
    EXPECT_EQ(classifyWindowChromeHit(layout, 200.0f, 80.0f), EWindowChromeHit::Client);
    EXPECT_EQ(classifyWindowChromeHit(layout, 4.0f, 4.0f), EWindowChromeHit::SystemButton);
}

TEST(GUIWindowChromeTest, HybridLayoutEmptyTitleIsDragExceptTabHoles)
{
    const FWindowChromeCapabilities caps = queryWindowChromeCapabilities();
    FWindowChromeLayout layout =
        makeWindowChromeLayout(EWindowChromeMode::Hybrid, caps, Extent2D{800, 600},
                               FWindowChromeInsets{.left = 78.0f, .top = 28.0f}, true);
    layout.titleClientHits.push_back(FWindowChromeRect{180.0f, 0.0f, 80.0f, 28.0f});
    EXPECT_EQ(classifyWindowChromeHit(layout, 10.0f, 10.0f), EWindowChromeHit::SystemButton);
    EXPECT_EQ(classifyWindowChromeHit(layout, 200.0f, 10.0f), EWindowChromeHit::Client);
    EXPECT_EQ(classifyWindowChromeHit(layout, 300.0f, 10.0f), EWindowChromeHit::Drag);
}

TEST(GUIWindowChromeTest, TitleTabBarRectIsClientTrailingGutterStaysDrag)
{
    const FWindowChromeCapabilities caps = queryWindowChromeCapabilities();
    FWindowChromeLayout layout =
        makeWindowChromeLayout(EWindowChromeMode::Hybrid, caps, Extent2D{800, 600},
                               FWindowChromeInsets{.left = 78.0f, .top = 28.0f}, true);
    layout.titleClientHits.push_back(layout.titleContent);
    EXPECT_EQ(classifyWindowChromeHit(layout, 10.0f, 10.0f), EWindowChromeHit::SystemButton);
    EXPECT_EQ(classifyWindowChromeHit(layout, layout.titleContent.x + 8.0f, 10.0f),
              EWindowChromeHit::Client);
    EXPECT_EQ(classifyWindowChromeHit(layout, layout.titleContent.x + layout.titleContent.width - 8.0f,
                                      10.0f),
              EWindowChromeHit::Client);
    EXPECT_EQ(classifyWindowChromeHit(layout, 750.0f, 10.0f), EWindowChromeHit::Drag);
    EXPECT_TRUE(layout.dragRegion.contains(750.0f, 10.0f));
}

TEST(GUIWindowChromeTest, TitleTabBarClientHitsUseBarRectNotButtons)
{
    const std::string surface =
        readEngineSource("Source/Applications/GameEditor/UI/Shell/EditorSurface.cpp");
    EXPECT_NE(surface.find("_pageTabBar->getLayoutRect()"), std::string::npos);
    EXPECT_NE(surface.find("bHideSourceWindowOnLeave"), std::string::npos);
    const std::string torn =
        readEngineSource("Source/Applications/GameEditor/UI/Dock/EditorNativeTearOff.cpp");
    EXPECT_NE(torn.find("state.tabBar->getLayoutRect()"), std::string::npos);
    EXPECT_NE(torn.find("bHideSourceWindowOnLeave"), std::string::npos);
    const std::string router =
        readEngineSource("Source/Framework/GUI/Host/GUIDragRouter.cpp");
    EXPECT_NE(router.find("syncHiddenSourceWindow"), std::string::npos);
    EXPECT_NE(router.find("isHidden()"), std::string::npos);
}

TEST(GUIWindowChromeTest, MacOsTitleDragUsesPerPixelWindowDragNotBackgroundMove)
{
    const std::string chromeCpp =
        readEngineSource("Source/Framework/GUI/Host/Window/GUIWindowChrome.cpp");
    EXPECT_NE(chromeCpp.find("setMovableByWindowBackground"), std::string::npos);
    EXPECT_NE(chromeCpp.find("performWindowDragWithEvent"), std::string::npos);
    const std::string cocoa =
        readEngineSource("Source/Framework/GUI/Host/Window/GUIWindowChromeCocoa.mm");
    EXPECT_NE(cocoa.find("performWindowDragWithEvent"), std::string::npos);
}

TEST(GUIWindowChromeTest, HybridLayoutZeroSafeAreaStillReservesTrafficLightsAndEmptyTitleDrag)
{
    const FWindowChromeCapabilities caps = queryWindowChromeCapabilities();
    const FWindowChromeLayout layout =
        makeWindowChromeLayout(EWindowChromeMode::Hybrid, caps, Extent2D{800, 600}, {}, true);

    EXPECT_GE(layout.contentInsets.top, 28.0f);
    EXPECT_GE(layout.contentInsets.left, 78.0f);
    EXPECT_EQ(classifyWindowChromeHit(layout, 10.0f, 10.0f), EWindowChromeHit::SystemButton);
    EXPECT_EQ(classifyWindowChromeHit(layout, 120.0f, 10.0f), EWindowChromeHit::Drag);
    EXPECT_EQ(classifyWindowChromeHit(layout, 750.0f, 10.0f), EWindowChromeHit::Drag);
    EXPECT_FALSE(layout.titleContent.empty());
    EXPECT_TRUE(layout.titleContent.contains(120.0f, 10.0f));
    EXPECT_FALSE(layout.titleContent.contains(10.0f, 10.0f));
}

TEST(GUIWindowChromeTest, ClientDrawnLayoutExposesResizeAndDrag)
{
    const FWindowChromeCapabilities caps = queryWindowChromeCapabilities();
    const FWindowChromeLayout layout =
        makeWindowChromeLayout(EWindowChromeMode::ClientDrawn, caps, Extent2D{400, 300}, {}, true);

    EXPECT_GT(layout.resizeBorder, 0.0f);
    EXPECT_EQ(classifyWindowChromeHit(layout, 2.0f, 2.0f), EWindowChromeHit::ResizeNW);
    EXPECT_EQ(classifyWindowChromeHit(layout, 398.0f, 2.0f), EWindowChromeHit::ResizeNE);
    EXPECT_EQ(classifyWindowChromeHit(layout, 200.0f, 10.0f), EWindowChromeHit::Drag);
    EXPECT_EQ(classifyWindowChromeHit(layout, 360.0f, 10.0f), EWindowChromeHit::Drag);
    EXPECT_EQ(classifyWindowChromeHit(layout, 200.0f, 80.0f), EWindowChromeHit::Client);
}

TEST(GUIWindowChromeTest, ExtraSessionUsesPlatformDefaultChrome)
{
    NamedWindowDelegate delegate;
    GUIWindowManager manager;
    ASSERT_TRUE(manager.init());

    FGUIWindowHostConfig config;
    config.title  = "MW-706";
    config.width  = 160;
    config.height = 120;
    const GUIWindowId id = manager.create(config, delegate);
    if (id == 0) {
        GTEST_SKIP() << "SDL native window create failed";
    }

    IGUIWindowSession* session = manager.findSession(id);
    ASSERT_NE(session, nullptr);
    EXPECT_EQ(session->chrome().mode, resolveWindowChromeMode(defaultWindowChromeMode()));
#if defined(__APPLE__)
    EXPECT_EQ(session->chrome().mode, EWindowChromeMode::Hybrid);
    EXPECT_TRUE(session->chrome().capabilities.hybridTitleContent);
#endif
    manager.destroySession(id);
}

TEST(GUIWindowChromeTest, TitleDoubleClickIgnoresClientAndSystemButtonHits)
{
    NamedWindowDelegate delegate;
    GUIWindowManager manager;
    ASSERT_TRUE(manager.init());

    FGUIWindowHostConfig config;
    config.title  = "TitleDblClick";
    config.width  = 800;
    config.height = 600;
    const GUIWindowId id = manager.create(config, delegate);
    if (id == 0) {
        GTEST_SKIP() << "SDL native window create failed";
    }

    IGUIWindowSession* session = manager.findSession(id);
    ASSERT_NE(session, nullptr);
    INativeWindow* native = session->nativeWindow();
    ASSERT_NE(native, nullptr);

    EXPECT_FALSE(handleWindowChromeTitleDoubleClick(*native, 10.0f, 10.0f));
    EXPECT_FALSE(handleWindowChromeTitleDoubleClick(*native, 200.0f, 80.0f));
#if defined(__APPLE__)
    EXPECT_EQ(session->chrome().mode, EWindowChromeMode::Hybrid);
    EXPECT_EQ(queryStoredWindowChromeHit(*native, 10.0f, 10.0f), EWindowChromeHit::SystemButton);
    EXPECT_EQ(queryStoredWindowChromeHit(*native, 200.0f, 10.0f), EWindowChromeHit::Drag);
    EXPECT_EQ(queryStoredWindowChromeHit(*native, 200.0f, 80.0f), EWindowChromeHit::Client);
#endif
    manager.destroySession(id);
}

TEST(GUIWindowChromeTest, RequestedClientDrawnDoesNotFallBackOnSupportedPlatforms)
{
    NamedWindowDelegate delegate;
    GUIWindowManager manager;
    ASSERT_TRUE(manager.init());

    FGUIWindowHostConfig config;
    config.title      = "MW-706-ClientDrawn";
    config.width      = 180;
    config.height     = 120;
    config.chromeMode = EWindowChromeMode::ClientDrawn;
    const GUIWindowId id = manager.create(config, delegate);
    if (id == 0) {
        GTEST_SKIP() << "SDL native window create failed";
    }

    IGUIWindowSession* session = manager.findSession(id);
    ASSERT_NE(session, nullptr);
    EXPECT_EQ(session->chrome().mode, EWindowChromeMode::ClientDrawn);
    EXPECT_GT(session->chrome().layout.resizeBorder, 0.0f);
    manager.destroySession(id);
}

TEST(GUIWindowChromeTest, DockAndEditorDoNotIncludePlatformNonClientApis)
{
    const char* files[] = {
        "Source/Framework/GUI/Runtime/Widgets/include/GUI/Widgets/Controls/DockSpace/DockContext.h",
        "Source/Framework/GUI/Runtime/Widgets/Controls/DockSpace/DockContext.cpp",
        "Source/Framework/GUI/Runtime/Widgets/Controls/DockSpace/DockSpace.cpp",
        "Source/Applications/GameEditor/include/GameEditor/UI/Shell/EditorSurface.h",
        "Source/Applications/GameEditor/UI/Shell/EditorSurface.cpp",
        "Source/Applications/GameEditor/include/GameEditor/UI/Dock/EditorDockWorkspace.h",
        "Source/Applications/GameEditor/UI/Dock/EditorDockWorkspace.cpp",
        "Source/Applications/GameEditor/include/GameEditor/UI/Shell/EditorTabSpawnerRegistry.h",
        "Source/Applications/GameEditor/UI/Shell/EditorTabSpawnerRegistry.cpp",
    };
    for (const char* file : files) {
        expectNoPlatformChrome(readEngineSource(file), file);
    }

    const std::string chromeH =
        readEngineSource("Source/Framework/GUI/Host/include/GUI/Host/GUIWindowChrome.h");
    EXPECT_NE(chromeH.find("EWindowChromeMode"), std::string::npos);
    EXPECT_NE(chromeH.find("must not call"), std::string::npos);
    EXPECT_NE(chromeH.find("queryWindowChromeLayout"), std::string::npos);
    EXPECT_EQ(chromeH.find("SDL_"), std::string::npos);

    const std::string chromeCpp =
        readEngineSource("Source/Framework/GUI/Host/Window/GUIWindowChrome.cpp");
    EXPECT_EQ(chromeCpp.find("SDL.h"), std::string::npos);
    EXPECT_EQ(chromeCpp.find("SDL_"), std::string::npos);
    EXPECT_EQ(chromeCpp.find("sdlWindowOf"), std::string::npos);
    EXPECT_EQ(chromeCpp.find("SDL_Window"), std::string::npos);
    EXPECT_NE(chromeCpp.find("setHitTest"), std::string::npos);
    EXPECT_NE(chromeCpp.find("setBordered"), std::string::npos);
    EXPECT_NE(chromeCpp.find("setMousePassthrough"), std::string::npos);

    const std::string placementCpp =
        readEngineSource("Source/Framework/GUI/Host/Window/GUIWindowPlacement.cpp");
    EXPECT_EQ(placementCpp.find("SDL.h"), std::string::npos);
    EXPECT_EQ(placementCpp.find("SDL_"), std::string::npos);
    EXPECT_NE(placementCpp.find("Os::displayCount"), std::string::npos);
    EXPECT_NE(placementCpp.find("Os::displayBounds"), std::string::npos);

    const std::string contextCpp =
        readEngineSource("Source/Applications/GameEditor/UI/Shell/EditorSurfaceContext.cpp");
    EXPECT_NE(contextCpp.find("queryWindowChromeLayout"), std::string::npos);
    EXPECT_EQ(contextCpp.find("queryWindowChromeInsets"), std::string::npos);
}

TEST(GUIWindowChromeTest, QueryAndApplyScreenPlacementUsesSizeNotOverlayCoords)
{
    GUIWindowManager manager;
    ASSERT_TRUE(manager.init());
    NamedWindowDelegate content;
    FGUIWindowHostConfig config;
    config.title  = "Placement";
    config.width  = 280;
    config.height = 160;
    const GUIWindowId id = manager.createSession(config, content, nullptr);
    if (id == 0) {
        GTEST_SKIP() << "SDL native window create failed";
    }
    IGUIWindowSession* session = manager.findSession(id);
    ASSERT_NE(session, nullptr);
    ASSERT_NE(session->nativeWindow(), nullptr);

    FWindowScreenPlacement queried = queryWindowScreenPlacement(*session->nativeWindow());
    EXPECT_EQ(queried.w, 280);
    EXPECT_EQ(queried.h, 160);

    FWindowScreenPlacement next = queried;
    next.w            = 360;
    next.h            = 200;
    next.x            = queried.x + 24;
    next.y            = queried.y + 16;
    next.bHasOrigin   = true;
    next.monitorIndex = queried.monitorIndex;
    ASSERT_TRUE(applyWindowScreenPlacement(*session->nativeWindow(), next));
    FWindowScreenPlacement applied = queryWindowScreenPlacement(*session->nativeWindow());
    EXPECT_EQ(applied.w, 360);
    EXPECT_EQ(applied.h, 200);

    FWindowScreenPlacement missingMonitor = applied;
    missingMonitor.x            = 12;
    missingMonitor.y            = 24;
    missingMonitor.bHasOrigin   = true;
    missingMonitor.monitorIndex = 999;
    missingMonitor.monitorName.clear();
    const FWindowPlacementApplyResult relocated =
        recoverWindowScreenPlacement(*session->nativeWindow(), missingMonitor);
    EXPECT_EQ(relocated.recovery, EWindowPlacementRecovery::Relocated);
    FWindowScreenPlacement recovered = queryWindowScreenPlacement(*session->nativeWindow());
    EXPECT_NE(glm::ivec2(recovered.x, recovered.y), glm::ivec2(12, 24));
    EXPECT_EQ(recovered.w, 360);
    EXPECT_EQ(recovered.h, 200);

    FWindowScreenPlacement unknownOrigin = recovered;
    unknownOrigin.x            = 12;
    unknownOrigin.y            = 24;
    unknownOrigin.bHasOrigin   = true;
    unknownOrigin.monitorIndex = -1;
    unknownOrigin.monitorName.clear();
    const FWindowPlacementApplyResult sizeOnly =
        recoverWindowScreenPlacement(*session->nativeWindow(), unknownOrigin);
    EXPECT_EQ(sizeOnly.recovery, EWindowPlacementRecovery::SizeOnly);
    FWindowScreenPlacement kept = queryWindowScreenPlacement(*session->nativeWindow());
    EXPECT_NE(glm::ivec2(kept.x, kept.y), glm::ivec2(12, 24));

    manager.destroySession(id);
}

} // namespace ya
