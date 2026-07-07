#include "Runtime/App/GUI/AppProfilingFacade.h"

#include "Runtime/App/App.h"

#include "Core/Profiling/Instrumentor.h"
#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Core/Profiling/Profiling.h"

#include "Runtime/App/RenderRuntime.h"

namespace ya
{

void AppProfilingFacade::render(App& app)
{
    if (ImGui::TreeNodeEx("Profiling", ImGuiTreeNodeFlags_DefaultOpen)) {
        renderContents(app);
        ImGui::TreePop();
    }
}

void AppProfilingFacade::renderContents(App& app)
{
    const auto compileMode = profiling::getCompileModeLabel();
    ImGui::Text("Compile Mode: %s", compileMode);

    auto state  = profiling::getRuntimeState();
    bool bDirty = false;

    if (ImGui::Checkbox("CPU Trace", &state.cpuTraceEnabled)) {
        bDirty = true;
    }
    if (ImGui::Checkbox("Perf Metrics", &state.perfMetricsEnabled)) {
        bDirty = true;
    }
    if (ImGui::Checkbox("Static Init", &state.staticInitEnabled)) {
        bDirty = true;
    }

    if (bDirty) {
        profiling::setRuntimeState(state);
        profiling::saveEditorConfig();
    }

    auto  paths    = profiling::getRuntimeSessionPaths();
    auto& cpuTrace = profiling::cpuTrace();
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

    auto&                        perf               = profiling::metrics();
    static constexpr size_t      WINDOW_SIZES[]     = {1, 10, 30, 60};
    static constexpr const char* WINDOW_LABELS[]    = {"Last", "10 frames", "30 frames", "60 frames"};
    int                          currentWindowIndex = 0;
    for (int i = 0; i < IM_ARRAYSIZE(WINDOW_SIZES); ++i) {
        if (perf.getAverageWindowSize() == WINDOW_SIZES[i]) {
            currentWindowIndex = i;
            break;
        }
    }
    if (ImGui::Combo("Metrics Average", &currentWindowIndex, WINDOW_LABELS, IM_ARRAYSIZE(WINDOW_LABELS))) {
        perf.setAverageWindowSize(WINDOW_SIZES[currentWindowIndex]);
    }

    const float frameCpuMs = perf.getDisplayValue(perf::sample::renderFrame(), perf::metric::cpuTimeMs());
    const float frameGpuMs = perf.getDisplayValue(perf::sample::renderFrame(), perf::metric::gpuTimeMs());
    ImGui::Text("Frame CPU: %.3f ms", frameCpuMs);
    ImGui::Text("Frame GPU: %.3f ms", frameGpuMs);

    if (auto* renderRuntime = app.getRenderRuntime()) {
        renderRuntime->renderProfilingDetailsGUI();
    }
}

} // namespace ya
