#pragma once

// ============================================================================
// FWorkbenchApp - GUIWorkbench presenter (gui-app-bootstrap Phase 3).
//
// Retain-mode shell built entirely from GUI framework controls (no ImGui,
// no Scene, no .yaui): toolbar commands | item list | live preview | 
// inspector. Layering (plan 4.2):
//
//   FWorkbenchWorkspace - app state / selection / commands (no GUI include)
//   FWorkbenchApp       - the ONLY layer knowing workspace + widget ids:
//                         maps workspace -> widget state and turns widget
//                         actions back into workspace mutations
//   Widget shell        - layout / paint / transient hover/focus/pressed
//   GUIAppHost          - window / input / snapshot / present (engine)
// ============================================================================

#include "GUI/App/GUIAppHost.h"
#include "GUI/Tooling/Workbench/WorkbenchSurface.h"

namespace guiworkbench
{

class FWorkbenchApp final : public ya::IGUIAppDelegate
{
  public:
    FWorkbenchSurface surface;
    bool              bSmokeActions = false;

    void buildUI(ya::WidgetTree& tree) override;
    void updateUI() override;
    void onRoutedEvent(const ya::Event& event, ya::EWidgetRouteResult result) override;
    [[nodiscard]] bool shouldRequestClose() const override
    {
        return bSmokeActions && surface.getSmokePassed();
    }
    [[nodiscard]] bool getSmokePassed() const { return surface.getSmokePassed(); }
};

} // namespace guiworkbench
