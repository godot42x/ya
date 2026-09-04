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

class RuntimeRenderSettingsSection final : public UICompoundWidget
{
  public:
    explicit RuntimeRenderSettingsSection(std::string name = "RuntimeRenderSettings");
    void sync(const App* app);

  protected:
    void construct() override;

  private:
    std::shared_ptr<UIText> _pipelineState;
    std::shared_ptr<UIText> _vsyncState;
    std::shared_ptr<UIDragFloat> _viewportScale;
    std::shared_ptr<UICheckBox> _vsync;
    std::shared_ptr<UIComboBox> _presentMode;
    std::shared_ptr<UIButton> _reload;
};
}
