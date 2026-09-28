#include "GameEditor/UI/Shell/EditorLaunchFlow.h"

#include "GameEditor/EditorLayer.h"
#include "GameEditor/UI/Shell/EditorOpenSplashPage.h"
#include "GameEditor/UI/Shell/EditorProjectBrowserPage.h"
#include "Core/Log.h"
#include "Core/Os/Os.h"
#include "RHI/NativeWindow.h"

#include <chrono>
#include <filesystem>

namespace ya
{

namespace
{

/// The browser window leads the flow: dialog-scale, normal chrome.
constexpr uint32_t kBrowserWindowWidth  = 880;
constexpr uint32_t kBrowserWindowHeight = 560;

/// The splash stays up at least this long so a fast open still reads as
/// "the editor is opening <project>" instead of a one-frame flash.
constexpr int kSplashMinDisplayMs = 400;

} // namespace

void EditorLaunchFlow::BrowserDelegate::buildUI(WidgetTree& tree)
{
    page.build(*flow->_layer, tree);
    page.onOpenRequested = [this](const std::string& projectPath) {
        if (flow) {
            flow->requestOpenProject(projectPath);
        }
    };
}

bool EditorLaunchFlow::requestOpenProject(const std::string& projectPath)
{
    if (_phase != EPhase::Browser) {
        return false;
    }
    _pendingOpen      = projectPath;
    _pendingSince     = std::chrono::steady_clock::now();
    _bClosingBrowser  = true;
    closeBrowserWindow();
    openSplashWindow(projectPath);
    _phase = EPhase::Splash;
    return true;
}

void EditorLaunchFlow::BrowserDelegate::updateUI()
{
    page.tick();
}

void EditorLaunchFlow::SplashDelegate::buildUI(WidgetTree& tree)
{
    page.build(projectName, tree);
}

void EditorLaunchFlow::begin(GUIWindowManager& windows, IRender* render, EditorLayer& layer, INativeWindow* mainWindow)
{
    _windows    = &windows;
    _render     = render;
    _layer      = &layer;
    _mainWindow = mainWindow;

    if (_layer->isProjectLoaded()) {
        // CLI launch with a project: no phases, the editor window leads.
        _phase = EPhase::Editor;
        return;
    }

    // Defensive: the shell window is created hidden for this flow already.
    if (_mainWindow) {
        _mainWindow->hide();
    }
    openBrowserWindow();
}

void EditorLaunchFlow::openBrowserWindow()
{
    _browserDelegate             = std::make_unique<BrowserDelegate>();
    _browserDelegate->flow       = this;
    _bClosingBrowser             = false;

    FGUIWindowHostConfig config;
    config.title        = "YA Editor";
    config.chromeMode   = EWindowChromeMode::Hybrid;
    config.width        = kBrowserWindowWidth;
    config.height       = kBrowserWindowHeight;
    config.bResizable   = true;
    int dx = 0, dy = 0, dw = 0, dh = 0;
    if (Os::displayBounds(0, dx, dy, dw, dh, /*usableWorkArea=*/false)) {
        config.bHasPosition = true;
        config.posX         = dx + (dw - static_cast<int>(kBrowserWindowWidth)) / 2;
        config.posY         = dy + (dh - static_cast<int>(kBrowserWindowHeight)) / 2;
    }
    _browserSession = _windows->createSession(config, *_browserDelegate, _render);
    if (_browserSession == 0) {
        YA_CORE_ERROR("EditorLaunchFlow: browser window failed to create; showing the shell window");
        if (_mainWindow) {
            _mainWindow->show();
        }
        _phase = EPhase::Editor;
        return;
    }
    _phase = EPhase::Browser;
    YA_CORE_INFO("EditorLaunchFlow: project browser window up (session {})", _browserSession);
}

void EditorLaunchFlow::openSplashWindow(const std::string& projectPath)
{
    _splashDelegate              = std::make_unique<SplashDelegate>();
    _splashDelegate->projectName = projectPath;

    FGUIWindowHostConfig config;
    config.title         = "YA Editor";
    config.width         = 800;
    config.height        = 600;
    // Fullscreen overlay on the primary display: borderless, on top,
    // transparent outside the banner, never taking focus.
    int dx = 0, dy = 0, dw = 0, dh = 0;
    if (Os::displayBounds(0, dx, dy, dw, dh, /*usableWorkArea=*/false)) {
        config.width        = static_cast<uint32_t>(dw);
        config.height       = static_cast<uint32_t>(dh);
        config.bHasPosition = true;
        config.posX         = dx;
        config.posY         = dy;
    }
    config.bBorderless   = true;
    config.bAlwaysOnTop  = true;
    config.bTransparent  = true;
    config.bNotFocusable = true;

    _splashSession = _windows->createSession(config, *_splashDelegate, _render);
    if (_splashSession == 0) {
        // No overlay to look at; run the load right away and show the editor.
        YA_CORE_WARN("EditorLaunchFlow: splash window failed to create; loading without it");
        const std::string path = projectPath;
        _pendingOpen.reset();
        _layer->requestOpenProject(path);
        _phase = EPhase::ShowEditor;
        return;
    }
    YA_CORE_INFO("EditorLaunchFlow: splash overlay up (session {})", _splashSession);
}

void EditorLaunchFlow::closeBrowserWindow()
{
    if (_browserSession != 0) {
        _windows->destroySession(_browserSession);
        _browserSession = 0;
    }
    _browserDelegate.reset();
}

void EditorLaunchFlow::closeSplashWindow()
{
    if (_splashSession != 0) {
        _windows->destroySession(_splashSession);
        _splashSession = 0;
    }
    _splashDelegate.reset();
}

void EditorLaunchFlow::tick()
{
    if (!_windows || !_layer) {
        return;
    }

    if (_phase == EPhase::Browser && _browserSession != 0) {
        // The browser is the only window of the phase: a user close there ends
        // the editor. (The flow's own close cleared the session handle first.)
        const IGUIWindowSession* session = _windows->findSession(_browserSession);
        if (session == nullptr || session->closeRequested()) {
            YA_CORE_INFO("EditorLaunchFlow: browser window closed; quitting");
            _layer->cmdRequestQuit();
            return;
        }
    }

    if (_phase == EPhase::Splash && _pendingOpen) {
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - _pendingSince).count();
        if (elapsedMs < kSplashMinDisplayMs) {
            return;
        }

        // The blocking load runs on the splash's last presented frame.
        const std::string path = *_pendingOpen;
        _pendingOpen.reset();
        closeSplashWindow();
        if (_layer->requestOpenProject(path)) {
            _phase = EPhase::ShowEditor;
            YA_CORE_INFO("EditorLaunchFlow: project loaded; showing the editor window");
        }
        else {
            YA_CORE_WARN("EditorLaunchFlow: project failed to open; back to the browser");
            openBrowserWindow();
        }
        return;
    }

    if (_phase == EPhase::ShowEditor) {
        // The chrome snapshot was built on the previous frame; showing now
        // presents a fully dressed editor window, never an empty one.
        if (_mainWindow) {
            _mainWindow->show();
        }
        _phase = EPhase::Editor;
    }
}

void EditorLaunchFlow::shutdown()
{
    closeBrowserWindow();
    closeSplashWindow();
    _pendingOpen.reset();
    _phase = EPhase::Idle;
}

} // namespace ya
