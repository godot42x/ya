#include "Core/Profiling/Profiling.h"

#include "Runtime/Application/App.h"

#include "Config/ConfigManager.h"

#include "Core/Log.h"
#include "Core/Profiling/Instrumentor.h"
#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <format>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>

namespace ya::profiling
{
namespace
{

constexpr const char* AUTOMATION_CONFIG_DOC_NAME      = "automation";

struct RuntimeArtifactState
{
    RuntimeSessionPaths paths;
    std::string         gpuCapturePath;
    std::string         passSummaryPath;
    std::string         screenshotPath;
    bool                bManifestDirty = false;
};

void writeJsonFile(const std::string& outputPath, const nlohmann::json& json);
nlohmann::json buildGpuSummaryJson(const RuntimeArtifactState& state);
nlohmann::json buildProfileSummaryJson(const RuntimeArtifactState& state);

enum class EArtifactFlushMode : uint8_t
{
    Async,
    Sync,
};

class RuntimeArtifactWriter
{
  public:
    void request(RuntimeArtifactState state)
    {
        std::unique_lock lock(_mutex);
        startWorkerIfNeeded(lock);
        _pendingState      = std::move(state);
        _pendingGeneration = ++_requestedGeneration;
        lock.unlock();
        _condition.notify_one();
    }

    void flush(RuntimeArtifactState state)
    {
        uint64_t generation = 0;
        {
            std::unique_lock lock(_mutex);
            startWorkerIfNeeded(lock);
            _pendingState      = std::move(state);
            generation         = ++_requestedGeneration;
            _pendingGeneration = generation;
        }
        _condition.notify_one();

        std::unique_lock lock(_mutex);
        _flushCondition.wait(lock, [this, generation]() {
            return _completedGeneration >= generation;
        });
    }

    void shutdown()
    {
        std::thread worker;
        {
            std::lock_guard lock(_mutex);
            _bStopping = true;
            worker     = std::move(_worker);
        }
        _condition.notify_one();
        if (worker.joinable()) {
            worker.join();
        }

        std::lock_guard lock(_mutex);
        _bStopping           = false;
        _requestedGeneration = 0;
        _completedGeneration = 0;
        _pendingGeneration   = 0;
        _pendingState.reset();
    }

  private:
    void startWorkerIfNeeded(std::unique_lock<std::mutex>&)
    {
        if (_worker.joinable()) {
            return;
        }

        _bStopping = false;
        _worker    = std::thread([this]() {
            workerLoop();
        });
    }

    void workerLoop()
    {
        while (true) {
            std::optional<RuntimeArtifactState> pendingState;
            uint64_t                            generation = 0;

            {
                std::unique_lock lock(_mutex);
                _condition.wait(lock, [this]() {
                    return _bStopping || _pendingState.has_value();
                });

                if (_bStopping && !_pendingState.has_value()) {
                    break;
                }

                pendingState      = std::move(_pendingState);
                generation        = _pendingGeneration;
                _pendingState.reset();
                _pendingGeneration = 0;
            }

            if (pendingState.has_value()) {
                writeJsonFile(pendingState->paths.gpuSummaryPath, buildGpuSummaryJson(*pendingState));
                writeJsonFile(pendingState->paths.profileSummaryPath, buildProfileSummaryJson(*pendingState));
            }

            {
                std::lock_guard lock(_mutex);
                _completedGeneration = std::max(_completedGeneration, generation);
            }
            _flushCondition.notify_all();
        }
    }

    std::mutex                        _mutex;
    std::condition_variable           _condition;
    std::condition_variable           _flushCondition;
    std::thread                       _worker;
    std::optional<RuntimeArtifactState> _pendingState;
    uint64_t                          _requestedGeneration = 0;
    uint64_t                          _completedGeneration = 0;
    uint64_t                          _pendingGeneration   = 0;
    bool                              _bStopping           = false;
};

RuntimeState& runtimeStateStorage()
{
#if defined(YA_PROFILING_ENABLED)
    static RuntimeState state{
        .cpuTraceEnabled    = true,
        .perfMetricsEnabled = true,
        .staticInitEnabled  = true,
    };
#else
    static RuntimeState state{
        .cpuTraceEnabled    = false,
        .perfMetricsEnabled = true,
        .staticInitEnabled  = true,
    };
#endif
    return state;
}

RuntimeArtifactState& runtimeArtifactStateStorage()
{
    static RuntimeArtifactState state;
    return state;
}

std::mutex& runtimeArtifactStateMutex()
{
    static std::mutex mutex;
    return mutex;
}

RuntimeArtifactWriter& runtimeArtifactWriter()
{
    static RuntimeArtifactWriter writer;
    return writer;
}

[[nodiscard]] bool normalizeRuntimeToggle(bool enabled)
{
    if constexpr (isAlwaysEnabled()) {
        return true;
    }
    if constexpr (isCompiledOut()) {
        return false;
    }
    return enabled;
}

void applyRuntimeState(RuntimeState& state)
{
    state.cpuTraceEnabled    = normalizeRuntimeToggle(state.cpuTraceEnabled);
    state.perfMetricsEnabled = normalizeRuntimeToggle(state.perfMetricsEnabled);
    state.staticInitEnabled  = normalizeRuntimeToggle(state.staticInitEnabled);
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

std::string getGpuSummaryOutputPath(const RuntimeSessionPaths& paths)
{
    return (std::filesystem::path(paths.outputDir) / "gpu_summary.json").string();
}

std::string getProfileSummaryOutputPath(const RuntimeSessionPaths& paths)
{
    return (std::filesystem::path(paths.outputDir) / "profile_summary.json").string();
}

float getMetricValue(FName sampleKey, FName metricKey)
{
    return profile::metrics().getLastValue(sampleKey, metricKey, 0.0f);
}

template <size_t N>
nlohmann::json buildCpuMetricMap(const std::array<std::pair<FName, const char*>, N>& metricDefs)
{
    nlohmann::json json = nlohmann::json::object();
    for (const auto& [sampleKey, label] : metricDefs) {
        json[label] = getMetricValue(sampleKey, perf::metric::cpuTimeMs());
    }
    return json;
}

nlohmann::json buildFrameCycleJson()
{
    static const std::array metricDefs = {
        std::pair{perf::sample::renderFrame(), "frameCpuMs"},
        std::pair{perf::sample::frameEventPump(), "eventPumpCpuMs"},
        std::pair{perf::sample::frameFpsControl(), "fpsControlCpuMs"},
        std::pair{perf::sample::frameLogic(), "logicCpuMs"},
        std::pair{perf::sample::frameRender(), "renderCpuMs"},
        std::pair{perf::sample::frameMainThreadCallbacks(), "mainThreadCallbacksCpuMs"},
        std::pair{perf::sample::frameAutomation(), "automationCpuMs"},
        std::pair{perf::sample::frameUnaccounted(), "unaccountedCpuMs"},
    };

    auto json          = buildCpuMetricMap(metricDefs);
    json["frameGpuMs"] = getMetricValue(perf::sample::renderFrame(), perf::metric::gpuTimeMs());
    return json;
}

nlohmann::json buildRenderStagesJson()
{
    static const std::array metricDefs = {
        std::pair{perf::sample::renderExtract(), "extractCpuMs"},
        std::pair{perf::sample::renderRuntime(), "runtimeCpuMs"},
        std::pair{perf::sample::renderPrepareFrame(), "prepareFrameCpuMs"},
        std::pair{perf::sample::renderWaitIdle(), "waitIdleCpuMs"},
        std::pair{perf::sample::renderBegin(), "beginCpuMs"},
        std::pair{perf::sample::renderWorld(), "worldCpuMs"},
        std::pair{perf::sample::renderViewportOverlay(), "viewportOverlayCpuMs"},
        std::pair{perf::sample::renderPostProcess(), "postProcessCpuMs"},
        std::pair{perf::sample::renderPresentation(), "presentationCpuMs"},
        std::pair{perf::sample::frameRenderCallbacks(), "renderCallbacksCpuMs"},
        std::pair{perf::sample::renderSubmit(), "submitCpuMs"},
        std::pair{perf::sample::appEventRoute(), "eventRouteCpuMs"},
        std::pair{perf::sample::appInputEvent(), "inputEventCpuMs"},
        std::pair{perf::sample::appUiEvent(), "uiEventCpuMs"},
        std::pair{perf::sample::appFileWatcher(), "fileWatcherCpuMs"},
        std::pair{perf::sample::shadowDirectional(), "shadowDirectionalCpuMs"},
        std::pair{perf::sample::shadowPoint(), "shadowPointCpuMs"},
        std::pair{perf::sample::shadowPointCull(), "shadowPointCullCpuMs"},
        std::pair{perf::sample::shadowPointFaceLoop(), "shadowPointFaceLoopCpuMs"},
        std::pair{perf::sample::shadowPointFaceDirect(), "shadowPointFaceDirectCpuMs"},
        std::pair{perf::sample::shadowPointFaceSkinned(), "shadowPointFaceSkinnedCpuMs"},
        std::pair{perf::sample::shadowPointDirectDrawStatic(), "shadowPointDirectDrawStaticCpuMs"},
    };
    return buildCpuMetricMap(metricDefs);
}

nlohmann::json buildSyncDiagnosticsJson()
{
    static const std::array metricDefs = {
        std::pair{perf::sample::vulkanWaitFence(), "waitFenceCpuMs"},
        std::pair{perf::sample::vulkanAcquire(), "acquireCpuMs"},
        std::pair{perf::sample::vulkanPresent(), "presentCpuMs"},
    };
    return buildCpuMetricMap(metricDefs);
}

void writeJsonFile(const std::string& outputPath, const nlohmann::json& json)
{
    std::filesystem::path path(outputPath);
    if (path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            YA_CORE_WARN("Failed to create profile output directory {}: {}", path.parent_path().string(), ec.message());
            return;
        }
    }

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream.is_open()) {
        YA_CORE_WARN("Failed to open profile output file: {}", outputPath);
        return;
    }
    stream << json.dump(2) << '\n';
}

nlohmann::json buildGpuSummaryJson(const RuntimeArtifactState& state)
{
    auto metricJson = nlohmann::json::array();
    const std::array metricDefs = {
        std::pair{perf::sample::renderFrame(), "Render/Frame"},
        std::pair{perf::sample::frameEventPump(), "Frame/EventPump"},
        std::pair{perf::sample::frameFpsControl(), "Frame/FpsControl"},
        std::pair{perf::sample::frameLogic(), "Frame/Logic"},
        std::pair{perf::sample::frameRender(), "Frame/Render"},
        std::pair{perf::sample::frameMainThreadCallbacks(), "Frame/MainThreadCallbacks"},
        std::pair{perf::sample::frameAutomation(), "Frame/Automation"},
        std::pair{perf::sample::frameUnaccounted(), "Frame/Unaccounted"},
        std::pair{perf::sample::renderExtract(), "Render/Extract"},
        std::pair{perf::sample::renderRuntime(), "Render/Runtime"},
        std::pair{perf::sample::renderPrepareFrame(), "Render/PrepareFrame"},
        std::pair{perf::sample::renderWaitIdle(), "Render/WaitIdle"},
        std::pair{perf::sample::renderBegin(), "Render/Begin"},
        std::pair{perf::sample::renderWorld(), "Render/World"},
        std::pair{perf::sample::renderViewportOverlay(), "Render/ViewportOverlay"},
        std::pair{perf::sample::renderPostProcess(), "Render/PostProcess"},
        std::pair{perf::sample::renderPresentation(), "Render/Presentation"},
        std::pair{perf::sample::frameRenderCallbacks(), "Frame/RenderCallbacks"},
        std::pair{perf::sample::renderSubmit(), "Render/Submit"},
        std::pair{perf::sample::appEventRoute(), "App/EventRoute"},
        std::pair{perf::sample::appInputEvent(), "App/InputEvent"},
        std::pair{perf::sample::appUiEvent(), "App/UIEvent"},
        std::pair{perf::sample::appFileWatcher(), "App/FileWatcher"},
        std::pair{perf::sample::vulkanWaitFence(), "Vulkan/WaitFence"},
        std::pair{perf::sample::vulkanAcquire(), "Vulkan/Acquire"},
        std::pair{perf::sample::vulkanPresent(), "Vulkan/Present"},
        std::pair{perf::sample::deferredTick(), "Deferred/Tick"},
        std::pair{perf::sample::deferredShadow(), "Deferred/Shadow"},
        std::pair{perf::sample::deferredGBuffer(), "Deferred/GBuffer"},
        std::pair{perf::sample::deferredDepthCopy(), "Deferred/DepthCopy"},
        std::pair{perf::sample::deferredLight(), "Deferred/Light"},
        std::pair{perf::sample::deferredOverlay(), "Deferred/Overlay"},
        std::pair{perf::sample::deferredLightPrepare(), "Deferred/LightPrepare"},
        std::pair{perf::sample::deferredLightExecute(), "Deferred/LightExecute"},
        std::pair{perf::sample::shadowDirectional(), "Shadow/Directional"},
        std::pair{perf::sample::shadowPoint(), "Shadow/Point"},
        std::pair{perf::sample::shadowPointCull(), "Shadow/Point/Cull"},
        std::pair{perf::sample::shadowPointFaceLoop(), "Shadow/Point/FaceLoop"},
        std::pair{perf::sample::shadowPointFaceDirect(), "Shadow/Point/FaceDirect"},
        std::pair{perf::sample::shadowPointFaceSkinned(), "Shadow/Point/FaceSkinned"},
        std::pair{perf::sample::shadowPointDirectDrawStatic(), "Shadow/Point/DirectDrawStatic"},
    };

    for (const auto& [sampleKey, label] : metricDefs) {
        const float cpuMs = getMetricValue(sampleKey, perf::metric::cpuTimeMs());
        const float gpuMs = getMetricValue(sampleKey, perf::metric::gpuTimeMs());
        if (cpuMs <= 0.0f && gpuMs <= 0.0f) {
            continue;
        }
        metricJson.push_back({
            {"sample", label},
            {"cpuMs", cpuMs},
            {"gpuMs", gpuMs},
        });
    }

    std::sort(metricJson.begin(), metricJson.end(), [](const nlohmann::json& a, const nlohmann::json& b)
              { return a.value("gpuMs", 0.0f) > b.value("gpuMs", 0.0f); });

    if (metricJson.size() > 5) {
        metricJson.erase(metricJson.begin() + 5, metricJson.end());
    }

    return {
        {"schemaVersion", 1},
        {"runId", state.paths.runId},
        {"gpuCapturePath", state.gpuCapturePath.empty() ? nlohmann::json(nullptr) : nlohmann::json(std::filesystem::path(state.gpuCapturePath).generic_string())},
        {"passSummary", state.passSummaryPath.empty() ? nlohmann::json(nullptr) : nlohmann::json(std::filesystem::path(state.passSummaryPath).generic_string())},
        {"topMetrics", metricJson},
        {"frameCycle", buildFrameCycleJson()},
        {"renderStages", buildRenderStagesJson()},
        {"syncDiagnostics", buildSyncDiagnosticsJson()},
    };
}

nlohmann::json buildProfileSummaryJson(const RuntimeArtifactState& state)
{
    const float frameCpuMs = getMetricValue(perf::sample::renderFrame(), perf::metric::cpuTimeMs());
    const float frameGpuMs = getMetricValue(perf::sample::renderFrame(), perf::metric::gpuTimeMs());

    nlohmann::json nextStep = nlohmann::json::array();
    if (frameCpuMs >= frameGpuMs && !state.paths.cpuProfilePath.empty()) {
        nextStep.push_back("open cpuProfile in speedscope and inspect hottest frame scopes");
    }
    if (frameGpuMs > frameCpuMs && !state.paths.gpuSummaryPath.empty()) {
        nextStep.push_back(state.passSummaryPath.empty()
                               ? "read gpuSummary topMetrics before opening full .rdc"
                               : "read gpuSummary topMetrics and passSummary before opening full .rdc");
    }
    if (!state.gpuCapturePath.empty()) {
        nextStep.push_back("only open .rdc when summary still cannot explain the bottleneck");
    }

    const char* bottleneck = "unknown";
    if (frameCpuMs > 0.0f || frameGpuMs > 0.0f) {
        bottleneck = frameCpuMs >= frameGpuMs ? "cpu" : "gpu";
    }

    return {
        {"schemaVersion", 1},
        {"runId", state.paths.runId},
        {"summary", {
            {"frameCpuMs", frameCpuMs},
            {"frameGpuMs", frameGpuMs},
            {"suspectedBottleneck", bottleneck},
        }},
        {"frameCycle", buildFrameCycleJson()},
        {"renderStages", buildRenderStagesJson()},
        {"syncDiagnostics", buildSyncDiagnosticsJson()},
        {"artifacts", {
            {"cpuProfile", state.paths.cpuProfilePath.empty() ? nlohmann::json(nullptr) : nlohmann::json(std::filesystem::path(state.paths.cpuProfilePath).generic_string())},
            {"gpuSummary", state.paths.gpuSummaryPath.empty() ? nlohmann::json(nullptr) : nlohmann::json(std::filesystem::path(state.paths.gpuSummaryPath).generic_string())},
            {"passSummary", state.passSummaryPath.empty() ? nlohmann::json(nullptr) : nlohmann::json(std::filesystem::path(state.passSummaryPath).generic_string())},
            {"gpuCapture", state.gpuCapturePath.empty() ? nlohmann::json(nullptr) : nlohmann::json(std::filesystem::path(state.gpuCapturePath).generic_string())},
            {"screenshot", state.screenshotPath.empty() ? nlohmann::json(nullptr) : nlohmann::json(std::filesystem::path(state.screenshotPath).generic_string())},
        }},
        {"nextStep", nextStep},
    };
}

std::optional<RuntimeArtifactState> captureDirtyArtifactSnapshot()
{
    std::lock_guard lock(runtimeArtifactStateMutex());
    auto&           state = runtimeArtifactStateStorage();
    if (!state.bManifestDirty || state.paths.outputDir.empty()) {
        return std::nullopt;
    }

    RuntimeArtifactState snapshot = state;
    snapshot.paths.gpuSummaryPath     = getGpuSummaryOutputPath(snapshot.paths);
    snapshot.paths.profileSummaryPath = getProfileSummaryOutputPath(snapshot.paths);
    state.paths.gpuSummaryPath        = snapshot.paths.gpuSummaryPath;
    state.paths.profileSummaryPath    = snapshot.paths.profileSummaryPath;
    state.bManifestDirty = false;
    return snapshot;
}

void flushRuntimeArtifactsInternal(EArtifactFlushMode mode)
{
    auto snapshot = captureDirtyArtifactSnapshot();
    if (!snapshot.has_value()) {
        if (mode == EArtifactFlushMode::Sync) {
            runtimeArtifactWriter().shutdown();
        }
        return;
    }

    if (mode == EArtifactFlushMode::Sync) {
        runtimeArtifactWriter().flush(std::move(*snapshot));
        runtimeArtifactWriter().shutdown();
        return;
    }

    runtimeArtifactWriter().request(std::move(*snapshot));
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

RuntimeState getRuntimeState()
{
    auto state = runtimeStateStorage();
    applyRuntimeState(state);
    return state;
}

void setRuntimeState(const RuntimeState& state)
{
    auto normalized = state;
    applyRuntimeState(normalized);
    runtimeStateStorage() = normalized;
    metrics().setEnabled(normalized.perfMetricsEnabled);
}

bool isCpuTraceEnabled()
{
    return getRuntimeState().cpuTraceEnabled;
}

void setCpuTraceEnabled(bool enabled)
{
    auto state            = getRuntimeState();
    state.cpuTraceEnabled = enabled;
    setRuntimeState(state);
}

bool isPerfMetricsEnabled()
{
    return getRuntimeState().perfMetricsEnabled;
}

void setPerfMetricsEnabled(bool enabled)
{
    auto state               = getRuntimeState();
    state.perfMetricsEnabled = enabled;
    setRuntimeState(state);
}

bool isStaticInitEnabled()
{
    return getRuntimeState().staticInitEnabled;
}

void setStaticInitEnabled(bool enabled)
{
    auto state             = getRuntimeState();
    state.staticInitEnabled = enabled;
    setRuntimeState(state);
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
        std::lock_guard lock(runtimeArtifactStateMutex());
        auto&           state                = runtimeArtifactStateStorage();
        state.paths.runId          = makeRunId(appDesc.profiling);
        state.paths.sessionName    = appDesc.profiling.profileSessionName;
        state.paths.outputDir      = resolveOutputDir(appDesc);
        state.paths.cpuProfilePath = resolveCpuProfilePath(appDesc, state.paths.runId);
        state.paths.gpuSummaryPath.clear();
        state.paths.profileSummaryPath.clear();
        state.gpuCapturePath.clear();
        state.passSummaryPath.clear();
        state.screenshotPath.clear();
        state.bManifestDirty = true;
        cpuProfilePath       = state.paths.cpuProfilePath;
    }

    if (appDesc.profiling.bCpuProfileOverridden || profiling::isProfileBuild()) {
        YA_PROFILE_SET_ENABLED(appDesc.profiling.bCpuProfileEnabled);
    }
    YA_PERF_SET_ENABLED(true);

    if (cpuTrace().IsSessionActive()) {
        return;
    }

    if (!YA_PROFILE_IS_ENABLED()) {
        flushRuntimeArtifactsInternal(EArtifactFlushMode::Sync);
        return;
    }

    cpuTrace().BeginSession(appDesc.profiling.profileSessionName, cpuProfilePath);
}

void endRuntimeSession()
{
    if (cpuTrace().IsSessionActive()) {
        cpuTrace().EndSession();
    }

    flushRuntimeArtifactsInternal(EArtifactFlushMode::Sync);
    YA_PROFILE_SET_ENABLED(false);
}

RuntimeSessionPaths getRuntimeSessionPaths()
{
    std::lock_guard lock(runtimeArtifactStateMutex());
    return runtimeArtifactStateStorage().paths;
}

void setGpuCapturePath(std::string path)
{
    std::lock_guard lock(runtimeArtifactStateMutex());
    auto&           state = runtimeArtifactStateStorage();
    if (state.gpuCapturePath == path) {
        return;
    }
    state.gpuCapturePath = std::move(path);
    state.bManifestDirty = true;
}

void setPassSummaryPath(std::string path)
{
    std::lock_guard lock(runtimeArtifactStateMutex());
    auto&           state = runtimeArtifactStateStorage();
    if (state.passSummaryPath == path) {
        return;
    }
    state.passSummaryPath = std::move(path);
    state.bManifestDirty  = true;
}

void setScreenshotPath(std::string path)
{
    std::lock_guard lock(runtimeArtifactStateMutex());
    auto&           state = runtimeArtifactStateStorage();
    if (state.screenshotPath == path) {
        return;
    }
    state.screenshotPath = std::move(path);
    state.bManifestDirty = true;
}

void flushRuntimeArtifacts()
{
    flushRuntimeArtifactsInternal(EArtifactFlushMode::Async);
}

Instrumentor& cpuTrace()
{
    return Instrumentor::Get();
}

PerfState& metrics()
{
    return PerfState::Get();
}

} // namespace ya::profiling
