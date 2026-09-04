#pragma once

#include "GUI/Widgets/CompoundWidget.h"

#include <memory>

namespace ya
{
struct App;
struct UIText;
struct UICheckBox;
struct UIComboBox;

class RuntimeProfilingSection final : public UICompoundWidget
{
  public:
    explicit RuntimeProfilingSection(std::string name = "RuntimeProfiling");
    void sync(const App* app);

  protected:
    void construct() override;

  private:
    std::shared_ptr<UIText> _compileMode;
    std::shared_ptr<UIText> _traceState;
    std::shared_ptr<UIText> _frameCpu;
    std::shared_ptr<UIText> _frameGpu;
    std::shared_ptr<UICheckBox> _cpuTrace;
    std::shared_ptr<UICheckBox> _perfMetrics;
    std::shared_ptr<UICheckBox> _staticInit;
    std::shared_ptr<UIComboBox> _averageWindow;
};
}
