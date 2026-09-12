#pragma once

#include "GUI/Widgets/CompoundWidget.h"

#include <memory>

namespace ya
{
struct App;
struct UIText;
struct UIButton;
struct UICheckBox;
struct UIComboBox;
struct UIDragFloat;
struct IRenderSurfaceContext;

class RuntimeRenderSettingsSection final : public UICompoundWidget
{
  public:
    explicit RuntimeRenderSettingsSection(std::string name = "RuntimeRenderSettings",
                                          App* app = nullptr,
                                          IRenderSurfaceContext* presentSurface = nullptr);
    void sync(const App* app, IRenderSurfaceContext* presentSurface);

  protected:
    void construct() override;

  private:
    App* _app = nullptr;
    IRenderSurfaceContext* _presentSurface = nullptr;
    std::shared_ptr<UIText> _pipelineState;
    std::shared_ptr<UIText> _vsyncState;
    std::shared_ptr<UIDragFloat> _viewportScale;
    std::shared_ptr<UICheckBox> _vsync;
    std::shared_ptr<UIComboBox> _presentMode;
    std::shared_ptr<UIButton> _reload;
};
}
