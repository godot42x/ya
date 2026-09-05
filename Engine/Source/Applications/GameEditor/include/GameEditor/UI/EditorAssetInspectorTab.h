#pragma once

#include "GUI/Widgets/CompoundWidget.h"

#include <memory>

namespace ya
{

struct EditorLayer;
struct UIElement;
struct WidgetTree;

/// Retained Asset Inspector tab. Inspected path lives on AssetInspectorPanel.
class EditorAssetInspectorTab : public UICompoundWidget
{
  public:
    explicit EditorAssetInspectorTab(EditorLayer& layer);

    void onAttached() override;
    void tick(float deltaSeconds) override;

  protected:
    void construct() override;

  private:
    EditorLayer* _layer = nullptr;
    std::shared_ptr<struct UIText> _pathText;
    std::shared_ptr<struct UIText> _statusText;
    std::shared_ptr<struct UIImage> _preview;

    void refresh();
};

} // namespace ya
