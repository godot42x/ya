#include "Editor/EditorProfilingSettings.h"

#include "Core/Config/ConfigManager.h"
#include "Core/Profiling/Profiling.h"

namespace ya::editor_profiling_settings
{

namespace
{

constexpr const char* CONFIG_DOCUMENT      = "editor";
constexpr const char* KEY_CPU_TRACE        = "profile.runtime.cpuTrace";
constexpr const char* KEY_PERF_METRICS     = "profile.runtime.perfMetrics";
constexpr const char* KEY_STATIC_INIT      = "profile.runtime.staticInit";

} // namespace

void load()
{
    auto& config = ConfigManager::get();
    if (!config.hasDocument(CONFIG_DOCUMENT)) {
        return;
    }

    auto state             = profiling::getRuntimeState();
    state.cpuTraceEnabled  = config.getOr<bool>(CONFIG_DOCUMENT, KEY_CPU_TRACE, state.cpuTraceEnabled);
    state.perfMetricsEnabled = config.getOr<bool>(CONFIG_DOCUMENT, KEY_PERF_METRICS, state.perfMetricsEnabled);
    state.staticInitEnabled = config.getOr<bool>(CONFIG_DOCUMENT, KEY_STATIC_INIT, state.staticInitEnabled);
    profiling::setRuntimeState(state);
}

void save()
{
    auto& config = ConfigManager::get();
    if (!config.hasDocument(CONFIG_DOCUMENT)) {
        return;
    }

    const auto state = profiling::getRuntimeState();
    ConfigManager::Editor(CONFIG_DOCUMENT)
        .set(KEY_CPU_TRACE, state.cpuTraceEnabled)
        .set(KEY_PERF_METRICS, state.perfMetricsEnabled)
        .set(KEY_STATIC_INIT, state.staticInitEnabled)
        .flush();
}

} // namespace ya::editor_profiling_settings
