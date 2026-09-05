#include "GameEditor/UI/EditorWorkbenchTab.h"

#include "GUI/Tooling/Workbench/WorkbenchSurface.h"
#include "GUI/Widgets/WidgetTree.h"

namespace ya
{

EditorWorkbenchTab::EditorWorkbenchTab() : UIPanel("WorkbenchHost")
{
    setStyleKey("panel.window");
}

EditorWorkbenchTab::~EditorWorkbenchTab() = default;

void EditorWorkbenchTab::buildWorkbench(WidgetTree& tree)
{
    if (_workbench) {
        return;
    }
    _workbench = std::make_unique<guiworkbench::FWorkbenchSurface>();
    _workbench->buildUI(tree, *this);
}

void EditorWorkbenchTab::tick(float)
{
    if (_workbench) {
        _workbench->updateUI();
    }
}

} // namespace ya
