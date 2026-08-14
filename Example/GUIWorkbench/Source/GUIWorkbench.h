#pragma once

// ============================================================================
// FWorkbenchApp - GUIWorkbench presenter (gui-app-bootstrap Phase 3).
//
// Retain-mode shell built entirely from GUI framework controls (no ImGui,
// no Scene, no .yaui). The app owns the demo content and registers it into
// the framework workbench shell:
//
//   FWorkbenchSurface  - framework shell: menu bar / tabs / status bar,
//                        built-in Editor reference page + automation
//   FWorkbenchApp      - example layer: demo pages (WorkbenchDemoPages),
//                        demo state + demo smoke automation
//   GUIApp + GUIWindowHost - app assembly / window input / snapshot / present
// ============================================================================

#include "GUI/Host/GUIApp.h"
#include "GUI/Tooling/Workbench/WorkbenchSurface.h"

#include "WorkbenchDemoPages.h"

#include <glm/glm.hpp>

namespace guiworkbench
{

class FWorkbenchApp final : public ya::IGUIAppDelegate
{
  public:
    FWorkbenchSurface surface;
    FDemoState        demoState;
    bool              bSmokeActions = false;
    std::string       startPageName;

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
    /// App-driven smoke steps for the registered demo pages (frames 3..13;
    /// frames >= 14 fall through to the shell's built-in Editor automation).
    bool runDemoAutomation(int frame);
    void dispatchPointer(const ya::Event& event, const glm::vec2& point);
    void dispatchKey(const ya::Event& event);

    ya::WidgetTree* _tree = nullptr;
};

} // namespace guiworkbench
