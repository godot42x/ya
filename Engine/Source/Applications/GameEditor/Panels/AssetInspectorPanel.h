#pragma once

#include <string>

namespace ya
{

struct EditorLayer;

/// Retained Asset Inspector selection state. ImGui render path was removed in
/// Phase 8E; EditorSurface consumes inspectedPath() for the retained tab.
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
