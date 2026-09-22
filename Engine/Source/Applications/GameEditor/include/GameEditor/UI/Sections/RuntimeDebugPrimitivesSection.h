#pragma once

#include "GUI/Widgets/CompoundWidget.h"

#include <memory>
#include <string>

namespace ya
{

struct App;
struct UIText;
struct UICheckBox;

/// Toggles and counters for the renderer's debug primitives, read and written
/// through the app's render services rather than the render device.
class RuntimeDebugPrimitivesSection final : public UICompoundWidget
{
  public:
    explicit RuntimeDebugPrimitivesSection(std::string name = "RuntimeDebugPrimitives");
    void sync(const App* app);

  protected:
    void construct() override;

  private:
    std::shared_ptr<UIText>    _counts;
    std::shared_ptr<UICheckBox> _enabled;
    std::shared_ptr<UICheckBox> _depth;
    std::shared_ptr<UICheckBox> _lines;
    std::shared_ptr<UICheckBox> _shapes;
};

} // namespace ya
