#include "GameEditor/Panels/GUIWorkbenchPanel.h"

#include "GameEditor/EditorLayer.h"
#include "GUI/Widgets/WidgetTree.h"

namespace ya
{

GUIWorkbenchPanel::GUIWorkbenchPanel(EditorLayer* owner)
    : _owner(owner)
{
}

void GUIWorkbenchPanel::ensureTree()
{
    if (_tree) {
        _tree->setLogicalExtent(_logicalExtent);
        return;
    }

    _tree = std::make_unique<WidgetTree>(_logicalExtent);
    _surface.buildUI(*_tree);
}

UIFrameSnapshot GUIWorkbenchPanel::buildSnapshot()
{
    if (!hasRenderableExtent()) {
        return {};
    }
    ensureTree();
    _tree->setLogicalExtent(_logicalExtent);
    _surface.updateUI();
    return _tree->buildSnapshot(UIFrameBuildContext{});
}

} // namespace ya
