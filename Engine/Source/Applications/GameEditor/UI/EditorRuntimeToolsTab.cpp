#include "GameEditor/UI/EditorRuntimeToolsTab.h"

#include "GUI/Declarative/Build.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Binding/ActionMap.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GameEditor/UI/RuntimeDebugPrimitivesSection.h"
#include "GameEditor/UI/RuntimeDiagnosticsSection.h"
#include "GameEditor/UI/RuntimeProfilingSection.h"
#include "GameEditor/UI/RuntimeRenderGraphSection.h"
#include "GameEditor/UI/RuntimeRenderTargetSection.h"
#include "GameRuntime/App.h"

#include <format>

namespace ya
{

EditorRuntimeToolsTab::EditorRuntimeToolsTab(ActionMap* actions, App* app)
    : UICompoundWidget("RuntimeToolsBody", "panel.canvas")
    , _actions(actions)
    , _app(app)
{
    enableTick();
}

void EditorRuntimeToolsTab::construct()
{
    auto status = ui::text("RuntimeToolsStatus").setText("Stopped").setStyleKey("text.header").share();
    auto frame = ui::text("RuntimeToolsFrame").setText("Frame 0").setStyleKey("text.muted").share();

    auto playBuilder = ui::button("RuntimeToolsPlay").child(ui::text("RuntimeToolsPlayLabel").setText("Play"));
    playBuilder.setOnClick([this]() {
        if (_actions) {
            (void)_actions->execute("runtime.play");
        }
    });
    auto play = playBuilder.share();

    auto simulateBuilder =
        ui::button("RuntimeToolsSimulate").child(ui::text("RuntimeToolsSimulateLabel").setText("Simulate"));
    simulateBuilder.setOnClick([this]() {
        if (_actions) {
            (void)_actions->execute("runtime.simulate");
        }
    });
    auto simulate = simulateBuilder.share();

    auto stopBuilder = ui::button("RuntimeToolsStop").child(ui::text("RuntimeToolsStopLabel").setText("Stop"));
    stopBuilder.setOnClick([this]() {
        if (_actions) {
            (void)_actions->execute("runtime.stop");
        }
    });
    auto stop = stopBuilder.share();
    auto diagnostics = std::make_shared<RuntimeDiagnosticsSection>();
    auto profiling = std::make_shared<RuntimeProfilingSection>();
    auto renderGraph = std::make_shared<RuntimeRenderGraphSection>();
    auto renderTargets = std::make_shared<RuntimeRenderTargetSection>();
    auto debugPrimitives = std::make_shared<RuntimeDebugPrimitivesSection>();

    _statusText = status;
    _frameText = frame;
    _playButton = play;
    _simulateButton = simulate;
    _stopButton = stop;
    _diagnostics = diagnostics;
    _profiling = profiling;
    _renderGraph = renderGraph;
    _renderTargets = renderTargets;
    _debugPrimitives = debugPrimitives;

    addDetachedChild(ui::scroll("RuntimeToolsScroll")
                         .setAxis(EScrollAxis::Vertical)
                         .child(ui::column("RuntimeToolsColumn")
                                    .setSpacing(8.0f)
                                    .setPadding({12.0f, 12.0f})
                                    .setStretchLastChild(false)
                                    .child(status)
                                    .child(frame)
                                    .child(play, FBoxSlotArgs{.preferredSize = {140.0f, 26.0f}})
                                    .child(simulate, FBoxSlotArgs{.preferredSize = {140.0f, 26.0f}})
                                    .child(stop, FBoxSlotArgs{.preferredSize = {140.0f, 26.0f}})
                                    .child(diagnostics)
                                    .child(profiling)
                                    .child(renderGraph)
                                    .child(renderTargets)
                                    .child(debugPrimitives)
                                    .release(),
                                ui::contentSlot()
                                    .hAlign(EUIOverlayAlignment::Fill)
                                    .vAlign(EUIOverlayAlignment::Start))
                         .release());
}

void EditorRuntimeToolsTab::onAttached()
{
    refresh();
}

void EditorRuntimeToolsTab::tick(float)
{
    refresh();
}

void EditorRuntimeToolsTab::refresh()
{
    if (!_app) {
        return;
    }
    const char* state = _app->isRuntimeMode() ? "Playing" : (_app->isSimulationMode() ? "Simulating" : "Stopped");
    _statusText->setText(state);
    _frameText->setText(std::format("Frame {}", _app->getFrameIndex()));
    _diagnostics->sync(_app);
    _profiling->sync(_app);
    _renderGraph->sync(_app);
    _renderTargets->sync(_app);
    _debugPrimitives->sync(_app);
    _playButton->setEnabled(_app->isStopped());
    _simulateButton->setEnabled(_app->isStopped());
    _stopButton->setEnabled(!_app->isStopped());
}

} // namespace ya
