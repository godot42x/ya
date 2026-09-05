#pragma once

#include <memory>

namespace ya
{

struct EditorLayer;
struct UIElement;
struct WidgetTree;

/// Retained Asset Inspector tab. Inspected path lives on AssetInspectorPanel;
/// this tab is the WidgetTree preview/status view.
class EditorAssetInspectorTab
{
  public:
    explicit EditorAssetInspectorTab(EditorLayer& layer) : _layer(&layer) {}

    [[nodiscard]] std::shared_ptr<UIElement> build(WidgetTree& tree);
    void sync();

  private:
    EditorLayer* _layer = nullptr;
    std::shared_ptr<struct UIText> _pathText;
    std::shared_ptr<struct UIText> _statusText;
    std::shared_ptr<struct UIImage> _preview;
};

} // namespace ya
