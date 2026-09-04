#pragma once

#include "GUI/Widgets/CompoundWidget.h"

#include <memory>

namespace ya
{

struct App;
struct UIText;
struct UICheckBox;
struct UIButton;
struct WidgetTree;

/// Retained, read-only summary of the runtime diagnostics service.
/// Actions that mutate RenderDoc state remain owned by RuntimeToolsPanel until
/// their retained command surface is migrated.
class RuntimeDiagnosticsSection final : public UICompoundWidget
{
  public:
    explicit RuntimeDiagnosticsSection(std::string name = "RuntimeDiagnostics");

    void sync(const App* app);

  protected:
    void construct() override;

  private:
    std::shared_ptr<UIText> _availability;
    std::shared_ptr<UIText> _dllPath;
    std::shared_ptr<UIText> _outputDir;
    std::shared_ptr<UIText> _lastCapture;
    std::shared_ptr<UIText> _captureState;
    std::shared_ptr<UICheckBox> _captureEnabled;
    std::shared_ptr<UICheckBox> _hudVisible;
    std::shared_ptr<UIButton> _captureNextFrame;
    std::shared_ptr<UIButton> _captureAfterFrames;
};

} // namespace ya
