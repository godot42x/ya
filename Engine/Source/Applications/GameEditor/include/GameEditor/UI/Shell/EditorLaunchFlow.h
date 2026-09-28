#pragma once

#include "GUI/Host/GUIAppDelegate.h"
#include "GUI/Host/GUIWindowManager.h"
#include "GameEditor/UI/Shell/EditorOpenSplashPage.h"
#include "GameEditor/UI/Shell/EditorProjectBrowserPage.h"

#include <chrono>
#include <memory>
#include <optional>
#include <string>

namespace ya
{

struct EditorLayer;
struct INativeWindow;
struct IRender;

/// Editor launch as a window-per-phase flow:
///
///   startup -> project browser window -> (Open Project) close self ->
///   fullscreen splash overlay -> (project loaded) close self ->
///   editor window (the app's main window, shown once its chrome is built)
///
/// The main window is created hidden for this flow (editor launched without a
/// project) and shown on the frame after the chrome snapshot exists, so no
/// phase ever flashes an empty window. With a project on the command line the
/// flow stays idle and the main window is visible from creation.
class EditorLaunchFlow
{
  public:
    void begin(GUIWindowManager& windows, IRender* render, EditorLayer& layer, INativeWindow* mainWindow);
    /// Per-frame, before the main surface ticks: consumes the pending open
    /// (the blocking project load runs here, with the splash's last presented
    /// frame on screen) and shows the editor window once the chrome exists.
    void tick();
    void shutdown();
    /// The Open action shared by the browser button and automation: switch to
    /// the splash phase and schedule the load (runs on a later tick). False
    /// when the flow is not in its browser phase (nothing was scheduled).
    [[nodiscard]] bool requestOpenProject(const std::string& projectPath);
    /// The sweep in EditorModule must not reap the flow's phase windows.
    [[nodiscard]] bool hostsSession(GUIWindowId id) const
    {
        return id != 0 && (id == _browserSession || id == _splashSession);
    }

  private:
    enum class EPhase
    {
        Idle,        // CLI launch with a project: main window visible from init
        Browser,     // the project browser window leads
        Splash,      // the splash overlay leads; load pending
        ShowEditor,  // chrome built on the hidden main window; show it next tick
        Editor,      // steady state
    };

    class BrowserDelegate final : public IGUIAppDelegate
    {
      public:
        EditorProjectBrowserPage page;
        EditorLaunchFlow*        flow = nullptr;

        void buildUI(WidgetTree& tree) override;
        void updateUI() override;
    };

    class SplashDelegate final : public IGUIAppDelegate
    {
      public:
        EditorOpenSplashPage page;
        std::string          projectName;

        void buildUI(WidgetTree& tree) override;
    };

    void openBrowserWindow();
    void openSplashWindow(const std::string& projectPath);
    void closeBrowserWindow();
    void closeSplashWindow();

    GUIWindowManager* _windows    = nullptr;
    IRender*          _render     = nullptr;
    EditorLayer*      _layer      = nullptr;
    INativeWindow*    _mainWindow = nullptr;
    std::unique_ptr<BrowserDelegate> _browserDelegate;
    std::unique_ptr<SplashDelegate>  _splashDelegate;
    GUIWindowId       _browserSession = 0;
    GUIWindowId       _splashSession  = 0;
    std::optional<std::string>      _pendingOpen;
    std::chrono::steady_clock::time_point _pendingSince{};
    EPhase            _phase            = EPhase::Idle;
    bool              _bClosingBrowser  = false; // flow-initiated, not a user close
};

} // namespace ya
