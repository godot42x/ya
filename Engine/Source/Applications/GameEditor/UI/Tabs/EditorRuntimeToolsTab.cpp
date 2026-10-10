#include "GameEditor/UI/Tabs/EditorRuntimeToolsTab.h"

#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "GUI/Declarative/Build.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Expander.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GameEditor/UI/Sections/RuntimeDebugPrimitivesSection.h"
#include "GameEditor/UI/Sections/RuntimeDiagnosticsSection.h"
#include "GameEditor/UI/Sections/RuntimeMemorySection.h"
#include "GameEditor/UI/Sections/RuntimeProfilingSection.h"
#include "GameEditor/UI/Sections/RuntimeRenderGraphSection.h"
#include "GameEditor/UI/Sections/RuntimeRenderTargetSection.h"
#include "GameEditor/UI/Shell/EditorStatReadouts.h"
#include "GameRuntime/App.h"

#include <format>
#include <memory>
#include <utility>

namespace ya
{
namespace
{

/// One KPI row: three cells sharing the width equally. Two of these stack into
/// the strip, so the six headline numbers stay put at a dock-tab width where a
/// single six-across row would overflow.
ui::UIContainerWidgetBuilder kpiRow(const char* key,
                                    const FStatReadout& first,
                                    const FStatReadout& second,
                                    const FStatReadout& third)
{
    return ui::row(key)
        .setSpacing(12.0f)
        .child(first.root, ui::boxSlot().fillWidth())
        .child(second.root, ui::boxSlot().fillWidth())
        .child(third.root, ui::boxSlot().fillWidth());
}

} // namespace

EditorRuntimeToolsTab::EditorRuntimeToolsTab(ActionMap* actions, App* app)
    : UICompoundWidget("RuntimeToolsBody", "panel.canvas")
    , _actions(actions)
    , _app(app)
{
    enableTick();
}

void EditorRuntimeToolsTab::construct()
{
    // State word + frame counter: a compact header line, not the panel's point.
    auto status = ui::text("RuntimeToolsStatus")
                      .setText("Stopped")
                      .setStyleKey("text.header")
                      .setVAlign(EWidgetAlignV::Center);
    auto frame = ui::text("RuntimeToolsFrame")
                     .setText("Frame 0")
                     .setStyleKey("text.muted")
                     .setHAlign(EWidgetAlignH::Right)
                     .setVAlign(EWidgetAlignV::Center);
    _statusText = status.share();
    _frameText  = frame.share();

    // The strip a reader watches: the six numbers that answer "is it fast and
    // is it holding memory", each a bright value under a muted label.
    const FStatReadout frameCell = makeStatCell("RuntimeKpiFrame", "Frame");
    const FStatReadout fpsCell   = makeStatCell("RuntimeKpiFps", "FPS");
    const FStatReadout gpuCell   = makeStatCell("RuntimeKpiGpu", "GPU");
    const FStatReadout gpuMemCell = makeStatCell("RuntimeKpiGpuMem", "GPU Mem");
    const FStatReadout rssCell   = makeStatCell("RuntimeKpiRss", "RSS");
    const FStatReadout peakCell  = makeStatCell("RuntimeKpiPeak", "Peak");
    _frameMs = frameCell.value;
    _fps     = fpsCell.value;
    _gpuMs   = gpuCell.value;
    _gpuMem  = gpuMemCell.value;
    _rss     = rssCell.value;
    _peak    = peakCell.value;

    auto diagnostics = std::make_shared<RuntimeDiagnosticsSection>();
    auto profiling = std::make_shared<RuntimeProfilingSection>();
    auto memory = std::make_shared<RuntimeMemorySection>();
    auto renderGraph = std::make_shared<RuntimeRenderGraphSection>();
    auto renderTargets = std::make_shared<RuntimeRenderTargetSection>();
    auto debugPrimitives = std::make_shared<RuntimeDebugPrimitivesSection>();

    _diagnostics = diagnostics;
    _profiling = profiling;
    _memory = memory;
    _renderGraph = renderGraph;
    _renderTargets = renderTargets;
    _debugPrimitives = debugPrimitives;

    // Each section folds into a collapsing header whose title carries the name
    // the section used to paint itself. The live-number sections open; the
    // reference and tool sections (render graph shape, render-target list,
    // RenderDoc, debug toggles) start folded, so they stop pushing the numbers
    // off the bottom of a dock tab.
    const auto section = [](const char* key, const char* title, bool expanded, UIElementRef body) {
        return ui::collapsingHeader(key)
            .setTitle(title)
            .setDisclosureKind(EDisclosureKind::Chevron)
            .setExpanded(expanded)
            .setSpacing(editor_density::kRowSpacing)
            .setPadding({editor_density::kPanelPadding, editor_density::kPanelPadding})
            .child(std::move(body));
    };

    auto sections = ui::column("RuntimeToolsColumn")
                        .setSpacing(editor_density::kSectionSpacing)
                        .setStretchLastChild(false)
                        .child(section("RuntimeSectionProfiling", "Profiling", true, profiling))
                        .child(section("RuntimeSectionMemory", "Memory", true, memory))
                        .child(section("RuntimeSectionRenderGraph", "Render Graph", false, renderGraph))
                        .child(section("RuntimeSectionRenderTargets", "Render Targets", false, renderTargets))
                        .child(section("RuntimeSectionDiagnostics", "Diagnostics", false, diagnostics))
                        .child(section("RuntimeSectionDebug", "Debug Primitives", false, debugPrimitives));

    addDetachedChild(ui::column("RuntimeToolsRoot")
                         .setSpacing(8.0f)
                         .setPadding({editor_density::kPanelPadding, editor_density::kPanelPadding})
                         .child(ui::row("RuntimeToolsHeader")
                                    .setSpacing(8.0f)
                                    .child(status)
                                    .child(frame, ui::boxSlot().fillWidth()))
                         .child(kpiRow("RuntimeKpiRow1", frameCell, fpsCell, gpuCell))
                         .child(kpiRow("RuntimeKpiRow2", gpuMemCell, rssCell, peakCell))
                         .child(ui::scroll("RuntimeToolsScroll")
                                    .setAxis(EScrollAxis::Vertical)
                                    .child(sections.release(),
                                           ui::contentSlot()
                                               .hAlign(EUIOverlayAlignment::Fill)
                                               .vAlign(EUIOverlayAlignment::Start)),
                                ui::boxSlot().fillHeight())
                         .release());
}

void EditorRuntimeToolsTab::onAttached()
{
    refresh();
}

void EditorRuntimeToolsTab::tick(float deltaSeconds)
{
    _frameSeconds = deltaSeconds;
    refresh();
}

void EditorRuntimeToolsTab::refresh()
{
    if (!_app) {
        return;
    }
    const char* state = _app->isRuntimeMode() ? "Playing" : (_app->isSimulationMode() ? "Simulating" : "Stopped");
    _statusText->setText(state);
    _frameText->setText(std::format("Frame {}", _app->getHostTick()));

    // The strip reads the live frame delta (the same quantity the Frame Stats
    // tab shows), the last completed frame's GPU time summed across passes, and
    // the memory readout the section below already sampled this frame -- so the
    // strip and the detail can never disagree.
    _frameMs->setText(_frameSeconds > 0.0f ? formatMs(_frameSeconds * 1000.0f) : std::string("-"));
    _fps->setText(_frameSeconds > 0.0f ? formatFps(1.0f / _frameSeconds) : std::string("-"));
    const float gpuMs = profiling::metrics().getDisplayValue(perf::sample::hostTick(), perf::metric::gpuTimeMs());
    _gpuMs->setText(gpuMs > 0.0f ? formatMs(gpuMs) : std::string("-"));

    _diagnostics->sync(_app);
    _profiling->sync(_app);
    _memory->sync(_app);
    _renderGraph->sync(_app);
    _renderTargets->sync(_app);
    _debugPrimitives->sync(_app);

    const FRuntimeMemoryReadout& memoryReadout = _memory->readout();
    _gpuMem->setText(memoryReadout.gpuSupported ? formatMiB(memoryReadout.gpuTotalBytes) : std::string("n/a"));
    _rss->setText(memoryReadout.processSupported ? formatMiB(memoryReadout.rssBytes) : std::string("n/a"));
    _peak->setText(memoryReadout.processSupported ? formatMiB(memoryReadout.peakBytes) : std::string("n/a"));
}

} // namespace ya
