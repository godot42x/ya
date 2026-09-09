#pragma once

// ============================================================================
// FWorkbenchApp - GUIWorkbench presenter (gui-app-bootstrap Phase 3).
//
// Retain-mode shell built entirely from GUI framework controls (no ImGui,
// no Scene, no UI documents). The app owns the demo content and registers it
// into the framework workbench shell:
//
//   FWorkbenchSurface  - framework shell: menu bar / tabs / status bar,
//                        built-in Editor reference page + automation
//   FWorkbenchApp      - example layer: demo pages (WorkbenchDemoPages),
//                        demo state + demo smoke automation
//   GUIApp + GUIWindowHost - app assembly / window input / snapshot / present
// ============================================================================

#include "GUI/Host/GUIApp.h"
#include "GUI/Tooling/Workbench/WorkbenchSurface.h"
#include "GUI/Widgets/Reactive.h"
#include "GUI/Widgets/Theme.h"

#include "WorkbenchDemoPages.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace guiworkbench
{

class ExtraOsWindowDemo final : public ya::IGUIAppDelegate
{
  public:
    std::string                                    title = "Extra";
    int                                            clicks = 0;
    std::shared_ptr<ya::Reactive<std::string>>     clickLabel =
        std::make_shared<ya::Reactive<std::string>>("Clicked: 0");
    std::shared_ptr<ya::Reactive<std::string>>     dropLabel =
        std::make_shared<ya::Reactive<std::string>>("Drop from the Windows page");

    void buildUI(ya::WidgetTree& tree) override;
    void updateUI() override {}
};

class FWorkbenchApp final : public ya::IGUIAppDelegate
{
  public:
    FWorkbenchSurface surface;
    FDemoState        demoState;
    bool              bSmokeActions = false;
    bool              bOpenExtraAtStart = false;
    std::string       startPageName;

    void bindHost(ya::GUIApp& app) { _guiApp = &app; }
    void openExtraWindow();
    void closeLatestExtra();

    void buildUI(ya::WidgetTree& tree) override;
    void updateUI() override;
    void onRoutedEvent(const ya::Event& event, ya::EWidgetRouteResult result) override;
    [[nodiscard]] bool shouldRequestClose() const override
    {
        return bSmokeActions && surface.getSmokePassed();
    }
    [[nodiscard]] bool getSmokePassed() const { return surface.getSmokePassed(); }

  private:
    void applyStartPage();
    /// App-driven smoke steps for the registered demo pages (frames 3..20;
    /// frames >= 21 fall through to the shell's built-in Editor automation).
    bool runDemoAutomation(int frame);
    void dispatchPointer(ya::WidgetTree& tree, const ya::Event& event, const glm::vec2& point);
    void dispatchPointer(const ya::Event& event, const glm::vec2& point);
    void dispatchKey(const ya::Event& event);

    ya::WidgetTree* _tree = nullptr;
    ya::GUIApp*     _guiApp = nullptr;
    std::vector<std::unique_ptr<ExtraOsWindowDemo>> _extraDemos;
    std::vector<ya::GUIWindowId>                    _extraIds;
    std::shared_ptr<ya::Reactive<std::string>>      _extraCountLabel =
        std::make_shared<ya::Reactive<std::string>>("Open extras: 0");

    void pruneClosedExtras();

    // Tree-level themes (style-system Phase 2/3). Owned by the app so they
    // outlive the tree; buildUI mounts _darkTheme via setTheme.
    std::shared_ptr<ya::UITheme> _darkTheme;
    std::shared_ptr<ya::UITheme> _lightTheme;
    bool                         _bDarkTheme = true;
};

} // namespace guiworkbench
