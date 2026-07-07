#pragma once

#include <cstdint>
#include <string>

// Unified compile-time profiling mode.
// Legacy YA_PROFILE_* / YA_PERF_* defines are normalized here.
#if !defined(YA_PROFILING_DISABLED) && !defined(YA_PROFILING_CONDITIONAL) && !defined(YA_PROFILING_ENABLED)
    #if defined(YA_PROFILE_ENABLED) || defined(YA_PERF_ENABLED)
        #define YA_PROFILING_ENABLED
    #elif defined(YA_PROFILE_CONDITIONAL) || defined(YA_PERF_CONDITIONAL)
        #define YA_PROFILING_CONDITIONAL
    #elif defined(YA_PROFILE_DISABLED) || defined(YA_PERF_DISABLED)
        #define YA_PROFILING_DISABLED
    #endif
#endif

#if !defined(YA_PROFILING_DISABLED) && !defined(YA_PROFILING_CONDITIONAL) && !defined(YA_PROFILING_ENABLED)
    #define YA_PROFILING_DISABLED
#endif

#if defined(YA_PROFILING_DISABLED)
    #ifndef YA_PROFILE_DISABLED
        #define YA_PROFILE_DISABLED
    #endif
    #ifndef YA_PERF_DISABLED
        #define YA_PERF_DISABLED
    #endif
#elif defined(YA_PROFILING_CONDITIONAL)
    #ifndef YA_PROFILE_CONDITIONAL
        #define YA_PROFILE_CONDITIONAL
    #endif
    #ifndef YA_PERF_CONDITIONAL
        #define YA_PERF_CONDITIONAL
    #endif
#elif defined(YA_PROFILING_ENABLED)
    #ifndef YA_PROFILE_ENABLED
        #define YA_PROFILE_ENABLED
    #endif
    #ifndef YA_PERF_ENABLED
        #define YA_PERF_ENABLED
    #endif
#endif

#if _WIN32
    #define YA_PRETTY_FUNCTION __FUNCSIG__
#elif __linux__
    #define YA_PRETTY_FUNCTION __PRETTY_FUNCTION__
#else
    #define YA_PRETTY_FUNCTION __func__
#endif

#define YA_CONCAT_IMPL(a, b) a##b
#define YA_CONCAT(a, b) YA_CONCAT_IMPL(a, b)

namespace ya
{
struct AppDesc;
struct Instrumentor;
struct PerfState;
} // namespace ya

namespace ya::profiling
{

enum class ECompileMode : uint8_t
{
    Disabled,
    Conditional,
    Enabled,
};

struct RuntimeState
{
    bool cpuTraceEnabled    = false;
    bool perfMetricsEnabled = false;
    bool staticInitEnabled  = false;
};

struct RuntimeSessionPaths
{
    std::string runId;
    std::string sessionName;
    std::string outputDir;
    std::string cpuProfilePath;
    std::string gpuSummaryPath;
    std::string profileSummaryPath;
};

[[nodiscard]] constexpr ECompileMode getCompileMode()
{
#if defined(YA_PROFILING_ENABLED)
    return ECompileMode::Enabled;
#elif defined(YA_PROFILING_CONDITIONAL)
    return ECompileMode::Conditional;
#else
    return ECompileMode::Disabled;
#endif
}

[[nodiscard]] constexpr bool isCompiledOut()
{
    return getCompileMode() == ECompileMode::Disabled;
}

[[nodiscard]] constexpr bool isRuntimeToggleSupported()
{
    return getCompileMode() == ECompileMode::Conditional;
}

[[nodiscard]] constexpr bool isAlwaysEnabled()
{
    return getCompileMode() == ECompileMode::Enabled;
}

[[nodiscard]] const char* getCompileModeLabel();

[[nodiscard]] RuntimeState getRuntimeState();
void setRuntimeState(const RuntimeState& state);

[[nodiscard]] bool isCpuTraceEnabled();
void setCpuTraceEnabled(bool enabled);

[[nodiscard]] bool isPerfMetricsEnabled();
void setPerfMetricsEnabled(bool enabled);

[[nodiscard]] bool isStaticInitEnabled();
void setStaticInitEnabled(bool enabled);

void loadEditorConfig();
void saveEditorConfig();

void beginRuntimeSession(const AppDesc& appDesc);
void endRuntimeSession();

void applyAppOverrides(AppDesc& appDesc);

[[nodiscard]] RuntimeSessionPaths getRuntimeSessionPaths();
void setGpuCapturePath(std::string path);
void setPassSummaryPath(std::string path);
void setScreenshotPath(std::string path);
void flushRuntimeArtifacts();

Instrumentor& cpuTrace();
PerfState&    metrics();

} // namespace ya::profiling
