#include "Core/Profiling/Profiling.h"

#include "GameRuntime/App.h"

#include "Core/Config/ConfigManager.h"

#include "Core/Log.h"
#include "Core/Profiling/Instrumentor.h"
#include "Core/Profiling/PerfState.h"

#include <cctype>
#include <chrono>
#include <filesystem>
#include <format>
#include <mutex>
#include <string_view>

namespace ya::profiling
{
namespace
{

constexpr const char* AUTOMATION_CONFIG_DOC_NAME = "automation";

RuntimeSessionPaths& runtimeSessionPathsStorage()
{
    static RuntimeSessionPaths paths;
    return paths;
}

std::mutex& runtimeSessionPathsMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::string sanitizePathSegment(std::string text)
{
    for (char& ch : text) {
        const bool bAlphaNum = std::isalnum(static_cast<unsigned char>(ch)) != 0;
        if (!bAlphaNum && ch != '-' && ch != '_') {
            ch = '_';
        }
    }
    if (text.empty()) {
        return "profile";
    }
    return text;
}

std::string makeRunId(const AppProfilingOptions& options)
{
    const auto      now      = std::chrono::system_clock::now();
    const auto      ms       = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    const std::time_t nowUtc = std::chrono::system_clock::to_time_t(now);

    std::tm tmValue{};
#if defined(_WIN32)
    localtime_s(&tmValue, &nowUtc);
#else
    localtime_r(&nowUtc, &tmValue);
#endif

    const auto subSecondMs = static_cast<long long>(ms % 1000);
    return std::format("{}-{:04d}{:02d}{:02d}-{:02d}{:02d}{:02d}-{:03d}",
                       sanitizePathSegment(options.profileSessionName),
                       tmValue.tm_year + 1900,
                       tmValue.tm_mon + 1,
                       tmValue.tm_mday,
                       tmValue.tm_hour,
                       tmValue.tm_min,
                       tmValue.tm_sec,
                       subSecondMs);
}

std::string resolveOutputDir(const AppDesc& appDesc)
{
    if (appDesc.profiling.cpuProfileOutputPath) {
        return std::filesystem::path(*appDesc.profiling.cpuProfileOutputPath).parent_path().string();
    }
    if (appDesc.automation.renderDocCapture) {
        return appDesc.renderDocCaptureOutputDir;
    }
    return "Engine/Saved/Profile";
}

std::string resolveCpuProfilePath(const AppDesc& appDesc, std::string_view runId)
{
    std::filesystem::path outputPath;
    if (appDesc.profiling.cpuProfileOutputPath) {
        outputPath = std::filesystem::path(*appDesc.profiling.cpuProfileOutputPath);
    }
    else {
        outputPath = std::filesystem::path(resolveOutputDir(appDesc)) / std::format("{}.cpu.speedscope.json", runId);
    }

    if (outputPath.extension() != ".json") {
        outputPath.replace_extension(".json");
    }
    return outputPath.string();
}

} // namespace

const char* getCompileModeLabel()
{
    switch (getCompileMode()) {
    case ECompileMode::Enabled:
        return "Always On";
    case ECompileMode::Conditional:
        return "Runtime Toggle";
    case ECompileMode::Disabled:
    default:
        return "Compiled Out";
    }
}

void applyAppOverrides(AppDesc& appDesc)
{
    auto& configManager = ConfigManager::get();
    if (!configManager.hasDocument(AUTOMATION_CONFIG_DOC_NAME)) {
        return;
    }

    if (!appDesc.profiling.bCpuProfileOverridden) {
        if (bool bCpuProfileEnabled = false;
            configManager.tryGet<bool>(AUTOMATION_CONFIG_DOC_NAME, "profile.cpu.enabled", bCpuProfileEnabled)) {
            appDesc.profiling.bCpuProfileEnabled    = bCpuProfileEnabled;
            appDesc.profiling.bCpuProfileOverridden = true;
        }
    }

    if (!appDesc.profiling.bCpuProfileOutputOverridden) {
        if (std::string cpuProfileOutputPath;
            configManager.tryGet<std::string>(AUTOMATION_CONFIG_DOC_NAME, "profile.cpu.output", cpuProfileOutputPath)) {
            appDesc.profiling.cpuProfileOutputPath = std::move(cpuProfileOutputPath);
        }
    }

    if (!appDesc.profiling.bProfileSessionNameOverridden) {
        if (std::string profileSessionName;
            configManager.tryGet<std::string>(AUTOMATION_CONFIG_DOC_NAME, "profile.sessionName", profileSessionName) && !profileSessionName.empty()) {
            appDesc.profiling.profileSessionName = std::move(profileSessionName);
        }
    }
}

void beginRuntimeSession(const AppDesc& appDesc)
{
    std::string cpuProfilePath;
    {
        std::lock_guard lock(runtimeSessionPathsMutex());
        auto&           paths = runtimeSessionPathsStorage();
        paths.runId           = makeRunId(appDesc.profiling);
        paths.sessionName     = appDesc.profiling.profileSessionName;
        paths.outputDir       = resolveOutputDir(appDesc);
        paths.cpuProfilePath  = resolveCpuProfilePath(appDesc, paths.runId);
        cpuProfilePath        = paths.cpuProfilePath;
    }

    // The struct default is the policy (on when the trace is compiled in).
    // CLI, automation config, and benchmark cpuTrace have already replaced it
    // when they apply. Compiled-out builds ignore the call.
    YA_PROFILE_SET_ENABLED(appDesc.profiling.bCpuProfileEnabled);
    YA_PERF_SET_ENABLED(true);

    if (cpuTrace().isSessionActive() || !YA_PROFILE_IS_ENABLED()) {
        return;
    }

    cpuTrace().beginSession(appDesc.profiling.profileSessionName, cpuProfilePath);
}

void endRuntimeSession()
{
    if (cpuTrace().isSessionActive()) {
        cpuTrace().endSession();
    }
    YA_PROFILE_SET_ENABLED(false);
}

RuntimeSessionPaths getRuntimeSessionPaths()
{
    std::lock_guard lock(runtimeSessionPathsMutex());
    return runtimeSessionPathsStorage();
}

} // namespace ya::profiling
