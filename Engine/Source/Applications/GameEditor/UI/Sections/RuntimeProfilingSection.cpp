#include "GameEditor/UI/Sections/RuntimeProfilingSection.h"

#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/Profiling.h"
#include "Core/Profiling/PerfState.h"
#include "GameRuntime/App.h"
#include "GUI/Widgets/Controls/CheckBox.h"
#include "GUI/Widgets/Controls/ComboBox.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Declarative/Build.h"
#include "GameEditor/UI/Shell/EditorTheme.h"

#include <algorithm>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace ya
{
namespace
{
std::shared_ptr<UICheckBox> makeCheckBox(const char* key, const char* label)
{
    auto box = std::make_shared<UICheckBox>(key);
    auto text = std::make_shared<UIText>(std::string(key) + "Label");
    text->setText(label);
    text->setStyleKey("text.muted");
    box->addDetachedChild(text);
    return box;
}
}

RuntimeProfilingSection::RuntimeProfilingSection(std::string name)
    : UICompoundWidget(std::move(name), "panel")
{
}

void RuntimeProfilingSection::construct()
{
    _compileMode = makeStatRow("RuntimeProfilingCompileMode", "Compile");
    _traceState = makeStatRow("RuntimeProfilingTraceState", "CPU Trace");
    _frameCpu = makeStatRow("RuntimeProfilingFrameCpu", "Frame CPU");
    _frameGpu = makeStatRow("RuntimeProfilingFrameGpu", "Frame GPU");
    _passTable = ui::text("RuntimeProfilingPassTable").setStyleKey("text.muted").share();
    _cpuTrace = makeCheckBox("RuntimeProfilingCpuTrace", "CPU Trace");
    _perfMetrics = makeCheckBox("RuntimeProfilingPerfMetrics", "Perf Metrics");
    _staticInit = makeCheckBox("RuntimeProfilingStaticInit", "Static Init");
    _averageWindow = std::make_shared<UIComboBox>("RuntimeProfilingAverageWindow");
    _averageWindow->setStyleKey(editorStyle(StyleKey::ComboBox));
    _averageWindow->_items = {"Last", "10 frames", "30 frames", "60 frames"};

    _cpuTrace->_onChanged = [](bool value) { profiling::setCpuTraceEnabled(value); };
    _perfMetrics->_onChanged = [](bool value) { profiling::setPerfMetricsEnabled(value); };
    _staticInit->_onChanged = [](bool value) { profiling::setStaticInitEnabled(value); };
    _averageWindow->_onSelectionChanged = [](int index) {
        constexpr size_t windows[] = {1, 10, 30, 60};
        if (index >= 0 && index < 4) profiling::metrics().setAverageWindowSize(windows[index]);
    };

    // No section title here: the tab supplies it as the collapsing header. The
    // rows are label -> value so the frame times line up under each other; the
    // switches and the window picker sit below as controls, then the pass table.
    auto rows = ui::column("RuntimeProfilingRows")
                    .setSpacing(editor_density::kRowSpacing)
                    .child(_compileMode.root)
                    .child(_traceState.root)
                    .child(_frameCpu.root)
                    .child(_frameGpu.root)
                    .child(_cpuTrace)
                    .child(_perfMetrics)
                    .child(_staticInit)
                    .child(_averageWindow, FBoxSlotArgs{.preferredSize = {180.0f, 26.0f}})
                    .child(_passTable, FBoxSlotArgs{.preferredSize = {260.0f, 200.0f}});
    addDetachedChild(rows.release());
}

void RuntimeProfilingSection::sync(const App*)
{
    const auto state = profiling::getRuntimeState();
    _compileMode.value->setText(profiling::getCompileModeLabel());
    _traceState.value->setText(profiling::cpuTrace().isSessionActive() ? "Active" : "Idle");
    _cpuTrace->setChecked(state.cpuTraceEnabled);
    _perfMetrics->setChecked(state.perfMetricsEnabled);
    _staticInit->setChecked(state.staticInitEnabled);
    const size_t window = profiling::metrics().getAverageWindowSize();
    const size_t windows[] = {1, 10, 30, 60};
    int selected = 0;
    for (int i = 0; i < 4; ++i) if (window == windows[i]) selected = i;
    _averageWindow->setSelectedIndex(selected, false);
    const auto& metrics = profiling::metrics();
    _frameCpu.value->setText(formatMs(metrics.getDisplayValue(perf::sample::hostTick(), perf::metric::cpuTimeMs())));
    _frameGpu.value->setText(formatMs(metrics.getDisplayValue(perf::sample::hostTick(), perf::metric::gpuTimeMs())));

    // Pass table: every pass the render graph timed in its last completed
    // frame, keyed by pass name. Empty until pass GPU timing produces results.
    std::vector<std::pair<std::string, float>> passes;
    if (state.passGpuTimingEnabled) {
        metrics.visitMetrics([&](const PerfMetricReading& reading) {
            if (reading.metricKey != perf::metric::gpuTimeMs() || reading.domainKey != perf::domain::render()) {
                return;
            }
            passes.emplace_back(std::string(reading.sampleKey.view()), reading.value);
        });
    }
    std::sort(passes.begin(), passes.end(), [](const auto& lhs, const auto& rhs) { return lhs.second > rhs.second; });
    std::string table;
    for (const auto& [name, timeMs] : passes) {
        table += std::format("{}  {:.3f} ms\n", name, timeMs);
    }
    _passTable->setText(table.empty() ? std::string("Pass GPU: (no data)") : std::format("Pass GPU:\n{}", table));
}
}
