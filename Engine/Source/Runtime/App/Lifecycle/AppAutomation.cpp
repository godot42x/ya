#include "Runtime/App/Lifecycle/AppAutomation.h"

#include "Runtime/App/App.h"
#include "Runtime/App/Utility/AppScreenshotCapture.h"

#include "Config/ConfigManager.h"

#include "Core/Debug/Instrumentor.h"
#include "Core/Debug/PerfKeys.h"
#include "Core/Debug/PerfState.h"
#include "Core/Log.h"

#include <nlohmann/json.hpp>

#include "ECS/Component/3D/EnvironmentLightingComponent.h"
#include "ECS/Component/3D/SkyboxComponent.h"
#include "ECS/Component/ModelComponent.h"

#include "Scene/Scene.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace ya
{
namespace
{
struct AppAutomationRuntimeState
{
    AppScreenshotCaptureState screenshot;
    std::string               runId;
    std::string               outputDir;
    std::string               cpuProfilePath;
    std::string               gpuSummaryPath;
    std::string               gpuCapturePath;
    std::string               passSummaryPath;
    const Scene*              stableScene          = nullptr;
    uint64_t                  warmupFrames         = 0;
    uint64_t                  stableFrames         = 0;
    bool                      bScreenshotRequested = false;
    bool                      bQuitDeferred        = false;
    bool                      bManifestDirty       = false;
};

AppAutomationRuntimeState& getAutomationRuntimeState()
{
    static AppAutomationRuntimeState state;
    return state;
}

std::string toLowerCopy(std::string_view text)
{
    std::string normalized(text);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch)
                   { return static_cast<char>(std::tolower(ch)); });
    return normalized;
}

bool tryParseScreenshotTargetValue(std::string_view text, EAutomationScreenshotTarget& outTarget)
{
    const std::string normalized = toLowerCopy(text);
    if (normalized == "viewport") {
        outTarget = EAutomationScreenshotTarget::Viewport;
        return true;
    }
    if (normalized == "editor") {
        outTarget = EAutomationScreenshotTarget::Editor;
        return true;
    }
    return false;
}

bool tryParseShadowQualityValue(std::string_view text, EShadowQuality::T& outQuality)
{
    const std::string normalized = toLowerCopy(text);
    if (normalized == "off") {
        outQuality = EShadowQuality::Off;
        return true;
    }
    if (normalized == "low") {
        outQuality = EShadowQuality::Low;
        return true;
    }
    if (normalized == "medium") {
        outQuality = EShadowQuality::Medium;
        return true;
    }
    if (normalized == "high") {
        outQuality = EShadowQuality::High;
        return true;
    }
    if (normalized == "ultra") {
        outQuality = EShadowQuality::Ultra;
        return true;
    }
    return false;
}

bool tryParseShadowFilterValue(std::string_view text, EShadowFilter::T& outFilter)
{
    const std::string normalized = toLowerCopy(text);
    if (normalized == "hard") {
        outFilter = EShadowFilter::Hard;
        return true;
    }
    if (normalized == "pcf_low" || normalized == "pcflow" || normalized == "pcf-low") {
        outFilter = EShadowFilter::PCF_Low;
        return true;
    }
    if (normalized == "pcf_high" || normalized == "pcfhigh" || normalized == "pcf-high") {
        outFilter = EShadowFilter::PCF_High;
        return true;
    }
    return false;
}

void loadScreenshotAutomationOverrides(AppDesc& appDesc)
{
    auto& configManager = ConfigManager::get();
    if (!configManager.hasDocument("automation") || appDesc.automation.bScreenshotTargetOverridden) {
        return;
    }

    if (std::string screenshotTargetText;
        configManager.tryGet<std::string>("automation", "screenshot.target", screenshotTargetText)) {
        EAutomationScreenshotTarget screenshotTarget = EAutomationScreenshotTarget::Viewport;
        if (tryParseScreenshotTargetValue(screenshotTargetText, screenshotTarget)) {
            appDesc.automation.screenshotTarget = screenshotTarget;
        }
        else {
            YA_CORE_WARN("Ignoring invalid automation screenshot target override: {}", screenshotTargetText);
        }
    }
}

void loadProfileAutomationOverrides(AppDesc& appDesc)
{
    auto& configManager = ConfigManager::get();
    if (!configManager.hasDocument("automation")) {
        return;
    }

    if (!appDesc.automation.bCpuProfileOverridden) {
        if (bool bCpuProfileEnabled = false;
            configManager.tryGet<bool>("automation", "profile.cpu.enabled", bCpuProfileEnabled)) {
            appDesc.automation.bCpuProfileEnabled = bCpuProfileEnabled;
        }
    }

    if (!appDesc.automation.bRenderDocCaptureOverridden) {
        if (bool bRenderDocCapture = false;
            configManager.tryGet<bool>("automation", "profile.gpu.renderdoc", bRenderDocCapture)) {
            appDesc.automation.renderDocCapture = bRenderDocCapture;
        }
    }

    if (!appDesc.bRenderDocOutputOverridden) {
        if (std::string renderDocOutputPath;
            configManager.tryGet<std::string>("automation", "profile.gpu.outputDir", renderDocOutputPath) && !renderDocOutputPath.empty()) {
            appDesc.renderDocCaptureOutputDir = std::move(renderDocOutputPath);
        }
    }

    if (!appDesc.automation.bCpuProfileOutputOverridden) {
        if (std::string cpuProfileOutputPath;
            configManager.tryGet<std::string>("automation", "profile.cpu.output", cpuProfileOutputPath)) {
            appDesc.automation.cpuProfileOutputPath = std::move(cpuProfileOutputPath);
        }
    }

    if (!appDesc.automation.bProfileSessionNameOverridden) {
        if (std::string profileSessionName;
            configManager.tryGet<std::string>("automation", "profile.sessionName", profileSessionName) && !profileSessionName.empty()) {
            appDesc.automation.profileSessionName = std::move(profileSessionName);
        }
    }
}

void loadShadowAutomationOverrides(AppDesc& appDesc)
{
    auto& configManager = ConfigManager::get();
    if (!configManager.hasDocument("automation")) {
        return;
    }

    if (std::string qualityText; configManager.tryGet<std::string>("automation", "shadow.quality", qualityText)) {
        EShadowQuality::T quality = EShadowQuality::Medium;
        if (tryParseShadowQualityValue(qualityText, quality)) {
            appDesc.automation.shadow.quality = quality;
        }
        else {
            YA_CORE_WARN("Ignoring invalid automation shadow quality override: {}", qualityText);
        }
    }
    else if (uint32_t qualityValue = 0; configManager.tryGet<uint32_t>("automation", "shadow.quality", qualityValue)) {
        if (qualityValue <= static_cast<uint32_t>(EShadowQuality::Ultra)) {
            appDesc.automation.shadow.quality = static_cast<EShadowQuality::T>(qualityValue);
        }
        else {
            YA_CORE_WARN("Ignoring invalid automation shadow quality override value: {}", qualityValue);
        }
    }

    if (bool directionalEnabled = false; configManager.tryGet<bool>("automation", "shadow.directionalEnabled", directionalEnabled)) {
        appDesc.automation.shadow.directionalEnabled = directionalEnabled;
    }
    if (bool pointLightEnabled = false; configManager.tryGet<bool>("automation", "shadow.pointLightEnabled", pointLightEnabled)) {
        appDesc.automation.shadow.pointLightEnabled = pointLightEnabled;
    }
    if (bool pointLightUseIndirect = false; configManager.tryGet<bool>("automation", "shadow.pointLightUseIndirect", pointLightUseIndirect)) {
        appDesc.automation.shadow.pointLightUseIndirect = pointLightUseIndirect;
    }
    if (uint32_t maxPointLightShadows = 0; configManager.tryGet<uint32_t>("automation", "shadow.maxPointLightShadows", maxPointLightShadows)) {
        appDesc.automation.shadow.maxPointLightShadows = std::min(maxPointLightShadows, static_cast<uint32_t>(MAX_POINT_LIGHTS));
    }

    if (std::string filterText; configManager.tryGet<std::string>("automation", "shadow.filter", filterText)) {
        EShadowFilter::T filter = EShadowFilter::Hard;
        if (tryParseShadowFilterValue(filterText, filter)) {
            appDesc.automation.shadow.filter = filter;
        }
        else {
            YA_CORE_WARN("Ignoring invalid automation shadow filter override: {}", filterText);
        }
    }
    else if (uint32_t filterValue = 0; configManager.tryGet<uint32_t>("automation", "shadow.filter", filterValue)) {
        if (filterValue <= static_cast<uint32_t>(EShadowFilter::PCF_High)) {
            appDesc.automation.shadow.filter = static_cast<EShadowFilter::T>(filterValue);
        }
        else {
            YA_CORE_WARN("Ignoring invalid automation shadow filter override value: {}", filterValue);
        }
    }

    if (float bias = 0.0f; configManager.tryGet<float>("automation", "shadow.bias", bias)) {
        appDesc.automation.shadow.bias = bias;
    }
    if (float normalBias = 0.0f; configManager.tryGet<float>("automation", "shadow.normalBias", normalBias)) {
        appDesc.automation.shadow.normalBias = normalBias;
    }
    if (float directionalDistance = 0.0f; configManager.tryGet<float>("automation", "shadow.directionalDistance", directionalDistance)) {
        appDesc.automation.shadow.directionalDistance = directionalDistance;
    }
}

void applyShadowAutomationOverrides(const AppAutomationShadowOverrides& overrides, ShadowSettings& shadowSettings)
{
    if (overrides.quality) {
        shadowSettings = ShadowSettings::fromQuality(*overrides.quality);
    }
    if (overrides.directionalEnabled) {
        shadowSettings.directionalEnabled = *overrides.directionalEnabled;
    }
    if (overrides.pointLightEnabled) {
        shadowSettings.pointLightEnabled = *overrides.pointLightEnabled;
    }
    if (overrides.pointLightUseIndirect) {
        shadowSettings.pointLightUseIndirect = *overrides.pointLightUseIndirect;
    }
    if (overrides.maxPointLightShadows) {
        shadowSettings.maxPointLightShadows = std::min(*overrides.maxPointLightShadows, static_cast<uint32_t>(MAX_POINT_LIGHTS));
    }
    if (overrides.filter) {
        shadowSettings.filter = *overrides.filter;
    }
    if (overrides.bias) {
        shadowSettings.bias = *overrides.bias;
    }
    if (overrides.normalBias) {
        shadowSettings.normalBias = *overrides.normalBias;
    }
    if (overrides.directionalDistance) {
        shadowSettings.directionalDistance = *overrides.directionalDistance;
    }
}

bool hasScreenshotAutomation(const AppAutomationOptions& automation)
{
    return automation.screenshotPath && !automation.screenshotPath->empty();
}

bool hasRenderDocAutomation(const AppAutomationOptions& automation)
{
    return automation.renderDocCapture;
}

bool isScreenshotTerminal(const AppAutomationRuntimeState& runtimeState)
{
    return runtimeState.screenshot.bCompleted || runtimeState.screenshot.bFailed;
}

bool shouldRequestQuitAfterFrame(const App& app)
{
    const AppAutomationOptions& automation = app.getDesc().automation;
    return automation.exitAfterFrame > 0 && app.getFrameIndex() >= automation.exitAfterFrame;
}

void resetAutomationStability(AppAutomationRuntimeState& runtimeState, const Scene* activeScene)
{
    runtimeState.stableScene  = activeScene;
    runtimeState.warmupFrames = 0;
    runtimeState.stableFrames = 0;
}

bool hasLoadingSkybox(const Scene& scene)
{
    for (const auto& [entity, skybox] : scene.getRegistry().view<SkyboxComponent>().each()) {
        (void)entity;
        if (skybox.isLoading()) {
            return true;
        }
    }
    return false;
}

bool hasLoadingEnvironmentLighting(const Scene& scene)
{
    for (const auto& [entity, environmentLighting] : scene.getRegistry().view<EnvironmentLightingComponent>().each()) {
        (void)entity;
        if (environmentLighting.isLoading()) {
            return true;
        }
    }
    return false;
}

bool hasPendingModelResolve(const Scene& scene)
{
    for (const auto& [entity, model] : scene.getRegistry().view<ModelComponent>().each()) {
        (void)entity;
        if (model.hasModelSource() && !model.isResolved()) {
            return true;
        }
    }
    return false;
}

bool isSceneStableForAutomation(const Scene& scene)
{
    return !hasLoadingSkybox(scene) &&
           !hasLoadingEnvironmentLighting(scene) &&
           !hasPendingModelResolve(scene);
}

bool isAutomationStableFrameReady(App& app)
{
    auto& runtimeState = getAutomationRuntimeState();

    Scene* activeScene = app.getSceneManager() ? app.getSceneManager()->getActiveScene() : nullptr;
    if (runtimeState.stableScene != activeScene) {
        resetAutomationStability(runtimeState, activeScene);
    }

    if (!activeScene || !activeScene->isValid()) {
        return false;
    }

    const AppAutomationOptions& automation = app.getDesc().automation;
    ++runtimeState.warmupFrames;
    if (runtimeState.warmupFrames <= automation.screenshotWarmupFrames) {
        return false;
    }

    if (!isSceneStableForAutomation(*activeScene)) {
        runtimeState.stableFrames = 0;
        return false;
    }

    ++runtimeState.stableFrames;
    const uint64_t settleFrames = automation.screenshotSettleFrames > 0 ? automation.screenshotSettleFrames : 1;
    return runtimeState.stableFrames >= settleFrames;
}

bool handleScreenshotAutomation(App& app, bool bStableFrameReady)
{
    auto& runtimeState = getAutomationRuntimeState();

    const AppAutomationOptions& automation = app.getDesc().automation;
    if (!hasScreenshotAutomation(automation)) {
        return false;
    }

    AppScreenshotCapture::tryFinalize(app, runtimeState.screenshot);
    if (runtimeState.bScreenshotRequested || isScreenshotTerminal(runtimeState)) {
        return !isScreenshotTerminal(runtimeState);
    }

    if (!bStableFrameReady) {
        return true;
    }

    runtimeState.bScreenshotRequested = AppScreenshotCapture::request(app,
                                                                      runtimeState.screenshot,
                                                                      *automation.screenshotPath,
                                                                      automation.screenshotTarget);
    if (runtimeState.bScreenshotRequested) {
        const uint64_t settleFrames = automation.screenshotSettleFrames > 0 ? automation.screenshotSettleFrames : 1;
        YA_CORE_INFO("Automation requested screenshot after {} warmup frames and {} stable frames: {}",
                     automation.screenshotWarmupFrames,
                     settleFrames,
                     *automation.screenshotPath);
    }

    return !isScreenshotTerminal(runtimeState);
}

bool handleRenderDocAutomation(App& app, bool bStableFrameReady)
{
    const AppAutomationOptions& automation = app.getDesc().automation;
    if (!hasRenderDocAutomation(automation)) {
        return false;
    }

    RenderRuntime* renderRuntime = app.getRenderRuntime();
    if (!renderRuntime) {
        return false;
    }

    if (renderRuntime->isAutomationRenderDocCapturePending()) {
        return true;
    }

    if (renderRuntime->isAutomationRenderDocCaptureTerminal()) {
        return false;
    }

    if (!bStableFrameReady) {
        return true;
    }

    const bool bRequested = renderRuntime->requestAutomationRenderDocCapture();
    if (bRequested) {
        const uint64_t settleFrames = automation.screenshotSettleFrames > 0 ? automation.screenshotSettleFrames : 1;
        YA_CORE_INFO("Automation requested a single RenderDoc capture after {} warmup frames and {} stable frames",
                     automation.screenshotWarmupFrames,
                     settleFrames);
    }

    return renderRuntime->isAutomationRenderDocCapturePending();
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
        return "automation";
    }
    return text;
}

std::string makeAutomationRunId(const AppAutomationOptions& automation)
{
    const auto now = std::chrono::system_clock::now();
    const auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return std::format("{}-{}", sanitizePathSegment(automation.profileSessionName), ms);
}

std::string getAutomationOutputDir(const AppAutomationOptions& automation, const AppDesc& appDesc)
{
    std::filesystem::path basePath;
    if (automation.renderDocCapture) {
        basePath = std::filesystem::path(appDesc.renderDocCaptureOutputDir);
    }
    else if (automation.cpuProfileOutputPath) {
        basePath = std::filesystem::path(*automation.cpuProfileOutputPath).parent_path();
    }

    if (basePath.empty()) {
        basePath = std::filesystem::path("Engine/Saved/Automation/Profile");
    }
    return basePath.string();
}

std::string getCpuProfileOutputPath(const AppAutomationOptions& automation, const AppDesc& appDesc)
{
    std::filesystem::path outputPath;
    if (automation.cpuProfileOutputPath) {
        outputPath = std::filesystem::path(*automation.cpuProfileOutputPath);
    }
    else {
        outputPath = std::filesystem::path(getAutomationOutputDir(automation, appDesc)) / std::format("{}.cpu.speedscope.json", makeAutomationRunId(automation));
    }

    if (outputPath.extension() != ".json") {
        outputPath.replace_extension(".json");
    }
    return outputPath.string();
}

std::string getGpuSummaryOutputPath(const AppAutomationRuntimeState& runtimeState)
{
    return (std::filesystem::path(runtimeState.outputDir) / "gpu_summary.json").string();
}

std::string getProfileManifestPath(const AppAutomationRuntimeState& runtimeState)
{
    return (std::filesystem::path(runtimeState.outputDir) / "profile_summary.json").string();
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
        std::pair{perf::sample::frameLogic(), "logicCpuMs"},
        std::pair{perf::sample::frameRender(), "renderCpuMs"},
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
        std::pair{perf::sample::renderFlushCallbacks(), "flushCallbacksCpuMs"},
        std::pair{perf::sample::renderSubmit(), "submitCpuMs"},
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

nlohmann::json buildGpuSummaryJson(const AppAutomationRuntimeState& runtimeState)
{
    auto metricJson = nlohmann::json::array();
    const std::array metricDefs = {
        std::pair{perf::sample::renderFrame(), "Render/Frame"},
        std::pair{perf::sample::frameLogic(), "Frame/Logic"},
        std::pair{perf::sample::frameRender(), "Frame/Render"},
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
        std::pair{perf::sample::renderFlushCallbacks(), "Render/FlushCallbacks"},
        std::pair{perf::sample::renderSubmit(), "Render/Submit"},
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
        {"runId", runtimeState.runId},
        {"gpuCapturePath", runtimeState.gpuCapturePath.empty() ? nlohmann::json(nullptr) : nlohmann::json(std::filesystem::path(runtimeState.gpuCapturePath).generic_string())},
        {"passSummary", runtimeState.passSummaryPath.empty() ? nlohmann::json(nullptr) : nlohmann::json(std::filesystem::path(runtimeState.passSummaryPath).generic_string())},
        {"topMetrics", metricJson},
        {"frameCycle", buildFrameCycleJson()},
        {"renderStages", buildRenderStagesJson()},
        {"syncDiagnostics", buildSyncDiagnosticsJson()},
    };
}

nlohmann::json buildProfileSummaryJson(const AppAutomationRuntimeState& runtimeState)
{
    const float frameCpuMs = getMetricValue(perf::sample::renderFrame(), perf::metric::cpuTimeMs());
    const float frameGpuMs = getMetricValue(perf::sample::renderFrame(), perf::metric::gpuTimeMs());

    nlohmann::json nextStep = nlohmann::json::array();
    if (frameCpuMs >= frameGpuMs && !runtimeState.cpuProfilePath.empty()) {
        nextStep.push_back("open cpuProfile in speedscope and inspect hottest frame scopes");
    }
    if (frameGpuMs > frameCpuMs && !runtimeState.gpuSummaryPath.empty()) {
        nextStep.push_back(runtimeState.passSummaryPath.empty()
                               ? "read gpuSummary topMetrics before opening full .rdc"
                               : "read gpuSummary topMetrics and passSummary before opening full .rdc");
    }
    if (!runtimeState.gpuCapturePath.empty()) {
        nextStep.push_back("only open .rdc when summary still cannot explain the bottleneck");
    }

    const char* bottleneck = "unknown";
    if (frameCpuMs > 0.0f || frameGpuMs > 0.0f) {
        bottleneck = frameCpuMs >= frameGpuMs ? "cpu" : "gpu";
    }

    return {
        {"schemaVersion", 1},
        {"runId", runtimeState.runId},
        {"summary", {
            {"frameCpuMs", frameCpuMs},
            {"frameGpuMs", frameGpuMs},
            {"suspectedBottleneck", bottleneck},
        }},
        {"frameCycle", buildFrameCycleJson()},
        {"renderStages", buildRenderStagesJson()},
        {"syncDiagnostics", buildSyncDiagnosticsJson()},
        {"artifacts", {
            {"cpuProfile", runtimeState.cpuProfilePath.empty() ? nlohmann::json(nullptr) : nlohmann::json(std::filesystem::path(runtimeState.cpuProfilePath).generic_string())},
            {"gpuSummary", runtimeState.gpuSummaryPath.empty() ? nlohmann::json(nullptr) : nlohmann::json(std::filesystem::path(runtimeState.gpuSummaryPath).generic_string())},
            {"passSummary", runtimeState.passSummaryPath.empty() ? nlohmann::json(nullptr) : nlohmann::json(std::filesystem::path(runtimeState.passSummaryPath).generic_string())},
            {"gpuCapture", runtimeState.gpuCapturePath.empty() ? nlohmann::json(nullptr) : nlohmann::json(std::filesystem::path(runtimeState.gpuCapturePath).generic_string())},
            {"screenshot", runtimeState.screenshot.outputPath.empty() ? nlohmann::json(nullptr) : nlohmann::json(std::filesystem::path(runtimeState.screenshot.outputPath).generic_string())},
        }},
        {"nextStep", nextStep},
    };
}

void flushProfileArtifacts()
{
    auto& runtimeState = getAutomationRuntimeState();
    if (!runtimeState.bManifestDirty || runtimeState.outputDir.empty()) {
        return;
    }

    runtimeState.gpuSummaryPath = getGpuSummaryOutputPath(runtimeState);
    writeJsonFile(runtimeState.gpuSummaryPath, buildGpuSummaryJson(runtimeState));
    writeJsonFile(getProfileManifestPath(runtimeState), buildProfileSummaryJson(runtimeState));
    runtimeState.bManifestDirty = false;
}

bool hasPendingAutomationWork(const App& app)
{
    auto&                       runtimeState = getAutomationRuntimeState();
    const AppAutomationOptions& automation   = app.getDesc().automation;

    const bool bScreenshotPending = hasScreenshotAutomation(automation) && !isScreenshotTerminal(runtimeState);

    bool bRenderDocPending = false;
    if (hasRenderDocAutomation(automation)) {
        if (const RenderRuntime* renderRuntime = app.getRenderRuntime()) {
            bRenderDocPending = !renderRuntime->isAutomationRenderDocCaptureTerminal();
        }
    }

    return bScreenshotPending || bRenderDocPending;
}
} // namespace

bool AppAutomation::shouldDeferQuit(const App& app)
{
    auto& runtimeState = getAutomationRuntimeState();
    if (!hasPendingAutomationWork(app)) {
        return false;
    }

    if (!runtimeState.bQuitDeferred) {
        YA_CORE_INFO("Deferring quit until automation work finishes");
        runtimeState.bQuitDeferred = true;
    }
    return true;
}

void AppAutomation::beginRuntimeProfiling(const AppAutomationOptions& automation)
{
    auto& runtimeState = getAutomationRuntimeState();
    const AppDesc& appDesc = App::get()->getDesc();

    runtimeState.runId          = makeAutomationRunId(automation);
    runtimeState.outputDir      = getAutomationOutputDir(automation, appDesc);
    runtimeState.cpuProfilePath = getCpuProfileOutputPath(automation, appDesc);
    runtimeState.gpuSummaryPath.clear();
    runtimeState.gpuCapturePath.clear();
    runtimeState.passSummaryPath.clear();
    runtimeState.bManifestDirty = true;

    YA_PROFILE_SET_ENABLED(automation.bCpuProfileEnabled);
    YA_PERF_SET_ENABLED(true);

    if (profile::cpuTrace().IsSessionActive()) {
        return;
    }

    if (!YA_PROFILE_IS_ENABLED()) {
        flushProfileArtifacts();
        return;
    }

    profile::cpuTrace().BeginSession(automation.profileSessionName, runtimeState.cpuProfilePath);
}

void AppAutomation::endRuntimeProfiling()
{
    if (profile::cpuTrace().IsSessionActive()) {
        profile::cpuTrace().EndSession();
    }

    flushProfileArtifacts();
    YA_PROFILE_SET_ENABLED(false);
}

void AppAutomation::applyStartupOverrides(AppDesc& appDesc)
{
    getAutomationRuntimeState() = {};
    loadScreenshotAutomationOverrides(appDesc);
    loadProfileAutomationOverrides(appDesc);
    loadShadowAutomationOverrides(appDesc);
    if (appDesc.automation.renderDocCapture) {
        appDesc.bEnableRenderDoc = true;
    }
    if (appDesc.automation.scenePath) {
        appDesc.defaultScenePath = appDesc.automation.scenePath;
    }
}

void AppAutomation::applyRuntimeOverrides(App& app)
{
    applyShadowAutomationOverrides(app.getDesc().automation.shadow, app.getShadowSettings());
}

void AppAutomation::recordPresentationCapture(App& app, ICommandBuffer* cmdBuf)
{
    auto& runtimeState = getAutomationRuntimeState();
    AppScreenshotCapture::recordPresentationCapture(app, runtimeState.screenshot, cmdBuf);
}

void AppAutomation::onFrameCompleted(App& app)
{
    auto&      runtimeState       = getAutomationRuntimeState();
    const bool bStableFrameReady  = isAutomationStableFrameReady(app);
    const bool bScreenshotPending = handleScreenshotAutomation(app, bStableFrameReady);
    const bool bRenderDocPending  = handleRenderDocAutomation(app, bStableFrameReady);
    const bool bAutomationPending = bScreenshotPending || bRenderDocPending;

    if (RenderRuntime* renderRuntime = app.getRenderRuntime()) {
        runtimeState.gpuCapturePath = renderRuntime->getAutomationRenderDocCapturePath();
        runtimeState.passSummaryPath = renderRuntime->getAutomationRenderDocPassSummaryPath();
    }

    runtimeState.bManifestDirty = true;
    if (runtimeState.bQuitDeferred && !bAutomationPending) {
        runtimeState.bQuitDeferred = false;
        YA_CORE_INFO("Continuing deferred shutdown after automation work finished");
        app.requestQuit();
        return;
    }

    flushProfileArtifacts();
    if (bAutomationPending || !shouldRequestQuitAfterFrame(app)) {
        return;
    }

    YA_CORE_INFO("Automation requested graceful shutdown after frame {}", app.getDesc().automation.exitAfterFrame);
    app.requestQuit();
}

} // namespace ya
