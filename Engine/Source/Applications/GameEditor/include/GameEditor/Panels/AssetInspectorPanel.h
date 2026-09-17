#pragma once

#include <string>

namespace ya
{

struct EditorLayer;

/// Inspected asset path for the retained Asset Inspector tab.
/// Not a WidgetTree view; ImGui render path was removed.
struct AssetInspectorPanel
{
  private:
    EditorLayer* _owner = nullptr;
    std::string  _inspectedPath;
    bool         _bVisible = false;

  public:
    explicit AssetInspectorPanel(EditorLayer* owner);

    AssetInspectorPanel(const AssetInspectorPanel&)            = delete;
    AssetInspectorPanel& operator=(const AssetInspectorPanel&) = delete;
    AssetInspectorPanel(AssetInspectorPanel&&)                 = delete;
    AssetInspectorPanel& operator=(AssetInspectorPanel&&)      = delete;

    void inspectTexture(const std::string& relativePath);
    void clear();

    bool isVisible() const { return _bVisible; }
    [[nodiscard]] const std::string& inspectedPath() const { return _inspectedPath; }
};

} // namespace ya
