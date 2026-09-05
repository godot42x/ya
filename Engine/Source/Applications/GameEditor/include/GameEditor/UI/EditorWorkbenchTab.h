#pragma once

#include "GUI/Widgets/Controls/Panel.h"

#include <memory>

namespace guiworkbench
{
class FWorkbenchSurface;
}

namespace ya
{

struct WidgetTree;

/// GUI workbench hosted as a dock tab. `FWorkbenchSurface::buildUI` must run
/// while this widget is temporarily attached; spawn() owns that dance.
class EditorWorkbenchTab : public UIPanel
{
  public:
    EditorWorkbenchTab();
    ~EditorWorkbenchTab() override;

    void buildWorkbench(WidgetTree& tree);
    [[nodiscard]] bool wantsTick() const override { return true; }
    void tick(float deltaSeconds) override;

  private:
    std::unique_ptr<guiworkbench::FWorkbenchSurface> _workbench;
};

} // namespace ya
