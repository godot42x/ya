#pragma once

#include "GUI/Widgets/CompoundWidget.h"

#include <memory>

namespace ya
{

struct UIElement;
struct WidgetTree;

/// Retained Runtime Tools tab. Play/stop chrome plus Runtime*Section widgets.
class EditorRuntimeToolsTab : public UICompoundWidget
{
  public:
    EditorRuntimeToolsTab();

    void onAttached() override;
    void tick(float deltaSeconds) override;

  protected:
    void construct() override;

  private:
    std::shared_ptr<struct UIText> _statusText;
    std::shared_ptr<struct UIText> _frameText;
    std::shared_ptr<struct UIButton> _playButton;
    std::shared_ptr<struct UIButton> _simulateButton;
    std::shared_ptr<struct UIButton> _stopButton;
    std::shared_ptr<class RuntimeDiagnosticsSection> _diagnostics;
    std::shared_ptr<class RuntimeRenderSettingsSection> _renderSettings;
    std::shared_ptr<class RuntimeProfilingSection> _profiling;
    std::shared_ptr<class RuntimeRenderGraphSection> _renderGraph;
    std::shared_ptr<class RuntimeRenderTargetSection> _renderTargets;
    std::shared_ptr<class RuntimeDebugPrimitivesSection> _debugPrimitives;

    void refresh();
};

} // namespace ya
