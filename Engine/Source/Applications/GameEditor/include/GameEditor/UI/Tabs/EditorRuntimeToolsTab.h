#pragma once

#include "GUI/Widgets/CompoundWidget.h"

#include <memory>

namespace ya
{

struct UIElement;
struct WidgetTree;
class ActionMap;
struct App;

/// Retained Runtime Tools tab. A pinned KPI strip (frame time, FPS, GPU time,
/// GPU memory, process RSS/peak) sits above a scroll of collapsible detail
/// sections -- so the handful of numbers a reader watches never scroll away
/// and never sit in the same visual weight as the prose around them.
///
/// Transport (Play / Simulate / Stop) is NOT here: it lives in the Play
/// toolbar tab, which is the one home for it, and the panel keeps only the
/// runtime state word so the KPI strip still says what it is showing.
/// Window session injects ActionMap and App; this tab does not call App::get().
class EditorRuntimeToolsTab : public UICompoundWidget
{
  public:
    EditorRuntimeToolsTab(ActionMap* actions = nullptr, App* app = nullptr);

    void onAttached() override;
    void tick(float deltaSeconds) override;

  protected:
    void construct() override;

  private:
    std::shared_ptr<struct UIText> _statusText;
    std::shared_ptr<struct UIText> _frameText;
    std::shared_ptr<struct UIText> _frameMs;
    std::shared_ptr<struct UIText> _fps;
    std::shared_ptr<struct UIText> _gpuMs;
    std::shared_ptr<struct UIText> _gpuMem;
    std::shared_ptr<struct UIText> _rss;
    std::shared_ptr<struct UIText> _peak;
    std::shared_ptr<class RuntimeDiagnosticsSection> _diagnostics;
    std::shared_ptr<class RuntimeProfilingSection> _profiling;
    std::shared_ptr<class RuntimeMemorySection> _memory;
    std::shared_ptr<class RuntimeRenderGraphSection> _renderGraph;
    std::shared_ptr<class RuntimeRenderTargetSection> _renderTargets;
    std::shared_ptr<class RuntimeDebugPrimitivesSection> _debugPrimitives;
    ActionMap* _actions = nullptr;
    App* _app = nullptr;
    float _frameSeconds = 0.0f;

    void refresh();
};

} // namespace ya
