#include "GameEditor/UI/RuntimeProfilingSection.h"

#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/Profiling.h"
#include "Core/Profiling/PerfState.h"
#include "GameRuntime/App.h"
#include "GUI/Widgets/Controls/CheckBox.h"
#include "GUI/Widgets/Controls/ComboBox.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Declarative/Build.h"

#include <format>

namespace ya
{
namespace
{
std::shared_ptr<UICheckBox> makeCheckBox(const char* key, const char* label)
{
    auto box = std::make_shared<UICheckBox>(key);
    auto text = std::make_shared<UIText>(std::string(key) + "Label");
    text->setText(label);
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
    _compileMode = ui::text("RuntimeProfilingCompileMode").share();
    _traceState = ui::text("RuntimeProfilingTraceState").share();
    _frameCpu = ui::text("RuntimeProfilingFrameCpu").share();
    _frameGpu = ui::text("RuntimeProfilingFrameGpu").share();
    _cpuTrace = makeCheckBox("RuntimeProfilingCpuTrace", "CPU Trace");
    _perfMetrics = makeCheckBox("RuntimeProfilingPerfMetrics", "Perf Metrics");
    _staticInit = makeCheckBox("RuntimeProfilingStaticInit", "Static Init");
    _averageWindow = std::make_shared<UIComboBox>("RuntimeProfilingAverageWindow");
    _averageWindow->_items = {"Last", "10 frames", "30 frames", "60 frames"};

    _cpuTrace->_onChanged = [](bool value) { profiling::setCpuTraceEnabled(value); };
    _perfMetrics->_onChanged = [](bool value) { profiling::setPerfMetricsEnabled(value); };
    _staticInit->_onChanged = [](bool value) { profiling::setStaticInitEnabled(value); };
    _averageWindow->_onSelectionChanged = [](int index) {
        constexpr size_t windows[] = {1, 10, 30, 60};
        if (index >= 0 && index < 4) profiling::metrics().setAverageWindowSize(windows[index]);
    };

    auto rows = ui::column("RuntimeProfilingRows")
                    .setSpacing(4.0f)
                    .child(ui::text("RuntimeProfilingHeader").setText("Profiling").setStyleKey("text.header"))
                    .child(_compileMode)
                    .child(_traceState)
                    .child(_cpuTrace)
                    .child(_perfMetrics)
                    .child(_staticInit)
                    .child(_averageWindow, FBoxSlotArgs{.preferredSize = {180.0f, 26.0f}})
                    .child(_frameCpu)
                    .child(_frameGpu);
    addDetachedChild(rows.release());
}

void RuntimeProfilingSection::sync(const App*)
{
    if (!_compileMode || !_traceState || !_frameCpu || !_frameGpu ||
        !_cpuTrace || !_perfMetrics || !_staticInit || !_averageWindow) {
        return;
    }
    const auto state = profiling::getRuntimeState();
    _compileMode->setText(std::format("Compile Mode: {}", profiling::getCompileModeLabel()));
    _traceState->setText(std::format("CPU Trace Session: {}", profiling::cpuTrace().isSessionActive() ? "Active" : "Idle"));
    _cpuTrace->setChecked(state.cpuTraceEnabled);
    _perfMetrics->setChecked(state.perfMetricsEnabled);
    _staticInit->setChecked(state.staticInitEnabled);
    const size_t window = profiling::metrics().getAverageWindowSize();
    const size_t windows[] = {1, 10, 30, 60};
    int selected = 0;
    for (int i = 0; i < 4; ++i) if (window == windows[i]) selected = i;
    _averageWindow->setSelectedIndex(selected, false);
    const auto& metrics = profiling::metrics();
    _frameCpu->setText(std::format("Frame CPU: {:.3f} ms", metrics.getDisplayValue(perf::sample::renderFrame(), perf::metric::cpuTimeMs())));
    _frameGpu->setText(std::format("Frame GPU: {:.3f} ms", metrics.getDisplayValue(perf::sample::renderFrame(), perf::metric::gpuTimeMs())));
}
}
