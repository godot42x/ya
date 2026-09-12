#include "GameEditor/UI/EditorLevelEditorTab.h"

#include "GUI/Declarative/Build.h"
#include "GUI/Widgets/Controls/DockSpace/DockContext.h"
#include "GUI/Widgets/Controls/DockSpace/DockSpace.h"

namespace ya
{

EditorLevelEditorTab::EditorLevelEditorTab(std::shared_ptr<FDockContext> nestedDock)
    : UICompoundWidget("LevelEditorBody", "panel.canvas")
    , _nestedDock(std::move(nestedDock))
{
}

void EditorLevelEditorTab::construct()
{
    if (!_nestedDock) {
        return;
    }
    _nestedDock->bAllowFloating = true;
    _nestedDock->bAllowTearOff  = true;
    addDetachedChild(ui::dockSpace("LevelEditorDock").setContext(_nestedDock).release());
}

} // namespace ya
