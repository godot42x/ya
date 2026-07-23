#include "Editor/Panels/RuntimeToolsPanelInternal.h"

namespace ya
{

void renderProfilingContent()
{
    const auto compileMode = profiling::getCompileModeLabel();
    ImGui::Text("Compile Mode: %s", compileMode);

    auto state  = profiling::getRuntimeState();
    bool bDirty = false;
    bDirty |= ImGui::Checkbox("CPU Trace", &state.cpuTraceEnabled);
    bDirty |= ImGui::Checkbox("Perf Metrics", &state.perfMetricsEnabled);
    bDirty |= ImGui::Checkbox("Static Init", &state.staticInitEnabled);
    if (bDirty) {
        profiling::setRuntimeState(state);
        editor_profiling_settings::save();
    }

    const auto paths     = profiling::getRuntimeSessionPaths();
    const auto& cpuTrace = profiling::cpuTrace();
    ImGui::Text("CPU Trace Session: %s", cpuTrace.IsSessionActive() ? "Active" : "Idle");
    if (!paths.sessionName.empty()) {
        ImGui::TextWrapped("Session: %s", paths.sessionName.c_str());
    }
    if (!paths.runId.empty()) {
        ImGui::TextWrapped("Run: %s", paths.runId.c_str());
    }
    if (!paths.outputDir.empty()) {
        ImGui::TextWrapped("Output Dir: %s", paths.outputDir.c_str());
    }
    if (!paths.cpuProfilePath.empty()) {
        ImGui::TextWrapped("CPU Trace Path: %s", paths.cpuProfilePath.c_str());
    }
    if (!paths.profileSummaryPath.empty()) {
        ImGui::TextWrapped("Summary Path: %s", paths.profileSummaryPath.c_str());
    }

    auto& perfState = profiling::metrics();
    static constexpr size_t WINDOW_SIZES[]       = {1, 10, 30, 60};
    static constexpr const char* WINDOW_LABELS[] = {"Last", "10 frames", "30 frames", "60 frames"};
    int currentWindowIndex = 0;
    for (int i = 0; i < IM_ARRAYSIZE(WINDOW_SIZES); ++i) {
        if (perfState.getAverageWindowSize() == WINDOW_SIZES[i]) {
            currentWindowIndex = i;
            break;
        }
    }
    if (ImGui::Combo("Metrics Average", &currentWindowIndex, WINDOW_LABELS, IM_ARRAYSIZE(WINDOW_SIZES))) {
        perfState.setAverageWindowSize(WINDOW_SIZES[currentWindowIndex]);
    }

    ImGui::Text("Frame CPU: %.3f ms", perfState.getDisplayValue(perf::sample::renderFrame(), perf::metric::cpuTimeMs()));
    ImGui::Text("Frame GPU: %.3f ms", perfState.getDisplayValue(perf::sample::renderFrame(), perf::metric::gpuTimeMs()));
}

} // namespace ya
