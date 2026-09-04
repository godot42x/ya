#include "GameEditor/Panels/AssetInspectorPanel.h"

#include "GameEditor/EditorLayer.h"

namespace ya
{

AssetInspectorPanel::AssetInspectorPanel(EditorLayer* owner) : _owner(owner) {}

void AssetInspectorPanel::inspectTexture(const std::string& relativePath)
{
    (void)_owner;
    if (relativePath == _inspectedPath && _bVisible) {
        return;
    }

    _inspectedPath = relativePath;
    _bVisible      = !relativePath.empty();
}

void AssetInspectorPanel::clear()
{
    _inspectedPath.clear();
    _bVisible = false;
}

} // namespace ya
