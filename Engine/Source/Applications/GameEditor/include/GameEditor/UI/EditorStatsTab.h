#pragma once

#include "GUI/Widgets/CompoundWidget.h"

#include <memory>

namespace ya
{

struct UIText;
struct EditorLayer;

/// Frame stats dock tab. FPS / viewport size are frame-semantic; tick only
/// while the tab is attached.
class EditorStatsTab : public UICompoundWidget
{
  public:
    explicit EditorStatsTab(EditorLayer& layer);

    void tick(float deltaSeconds) override;

  protected:
    void construct() override;

  private:
    EditorLayer* _layer = nullptr;
    std::shared_ptr<UIText> _statsText;
};

} // namespace ya
