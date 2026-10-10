#pragma once

#include "GUI/Widgets/CompoundWidget.h"
#include "GameEditor/UI/Shell/EditorStatReadouts.h"

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
    FStatReadout _compileMode;
    FStatReadout _traceState;
    FStatReadout _frameCpu;
    FStatReadout _frameGpu;
    std::shared_ptr<UIText> _passTable;
    std::shared_ptr<UICheckBox> _cpuTrace;
    std::shared_ptr<UICheckBox> _perfMetrics;
    std::shared_ptr<UICheckBox> _staticInit;
    std::shared_ptr<UIComboBox> _averageWindow;
};
}
