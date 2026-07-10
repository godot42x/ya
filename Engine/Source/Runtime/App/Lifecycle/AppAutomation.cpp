#include "Runtime/App/Lifecycle/AppAutomation.h"

#include "Runtime/App/App.h"
#include "Runtime/App/RenderRuntime.h"
#include "Runtime/App/Utility/AppScreenshotCapture.h"

#include "Config/ConfigManager.h"

#include "Core/Log.h"
#include "Core/Profiling/Profiling.h"

#include "ECS/Component/3D/EnvironmentLightingComponent.h"
#include "ECS/Component/3D/SkyboxComponent.h"
#include "ECS/Component/ModelComponent.h"

#include "Scene/Scene.h"

#include <algorithm>
#include <string_view>

namespace ya
{
namespace
{
struct AppAutomationRuntimeState
{
    AppScreenshotCaptureState screenshot;
    const Scene*              stableScene          = nullptr;
    uint64_t                  warmupFrames         = 0;
    uint64_t                  stableFrames         = 0;
    bool                      bScreenshotRequested = false;
    bool                      bQuitDeferred        = false;
};

constexpr const char* AUTOMATION_CONFIG_DOC_NAME = "automation";
constexpr const char* AUTOMATION_CONFIG_PATH     = "Engine/Saved/Config/Automation.json";

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
    if (!configManager.hasDocument(AUTOMATION_CONFIG_DOC_NAME) || appDesc.automation.bScreenshotTargetOverridden) {
        return;
    }

    if (std::string screenshotTargetText;
        configManager.tryGet<std::string>(AUTOMATION_CONFIG_DOC_NAME, "screenshot.target", screenshotTargetText)) {
        EAutomationScreenshotTarget screenshotTarget = EAutomationScreenshotTarget::Viewport;
        if (tryParseScreenshotTargetValue(screenshotTargetText, screenshotTarget)) {
            appDesc.automation.screenshotTarget = screenshotTarget;
        }
        else {
            YA_CORE_WARN("Ignoring invalid automation screenshot target override: {}", screenshotTargetText);
        }
    }
}

void loadRenderDocAutomationOverrides(AppDesc& appDesc)
{
    auto& configManager = ConfigManager::get();
    if (!configManager.hasDocument(AUTOMATION_CONFIG_DOC_NAME)) {
        return;
    }

    if (!appDesc.automation.bRenderDocCaptureOverridden) {
        if (bool bRenderDocCapture = false;
            configManager.tryGet<bool>(AUTOMATION_CONFIG_DOC_NAME, "profile.gpu.renderdoc", bRenderDocCapture)) {
            appDesc.automation.renderDocCapture = bRenderDocCapture;
        }
    }

    if (!appDesc.bRenderDocOutputOverridden) {
        if (std::string renderDocOutputPath;
            configManager.tryGet<std::string>(AUTOMATION_CONFIG_DOC_NAME, "profile.gpu.outputDir", renderDocOutputPath) && !renderDocOutputPath.empty()) {
            appDesc.renderDocCaptureOutputDir = std::move(renderDocOutputPath);
        }
    }
}

void loadShadowAutomationOverrides(AppDesc& appDesc)
{
    auto& configManager = ConfigManager::get();
    if (!configManager.hasDocument(AUTOMATION_CONFIG_DOC_NAME)) {
        return;
    }

    if (std::string qualityText; configManager.tryGet<std::string>(AUTOMATION_CONFIG_DOC_NAME, "shadow.quality", qualityText)) {
        EShadowQuality::T quality = EShadowQuality::Medium;
        if (tryParseShadowQualityValue(qualityText, quality)) {
            appDesc.automation.shadow.quality = quality;
        }
        else {
            YA_CORE_WARN("Ignoring invalid automation shadow quality override: {}", qualityText);
        }
    }
    else if (uint32_t qualityValue = 0; configManager.tryGet<uint32_t>(AUTOMATION_CONFIG_DOC_NAME, "shadow.quality", qualityValue)) {
        if (qualityValue <= static_cast<uint32_t>(EShadowQuality::Ultra)) {
            appDesc.automation.shadow.quality = static_cast<EShadowQuality::T>(qualityValue);
        }
        else {
            YA_CORE_WARN("Ignoring invalid automation shadow quality override value: {}", qualityValue);
        }
    }

    if (bool directionalEnabled = false; configManager.tryGet<bool>(AUTOMATION_CONFIG_DOC_NAME, "shadow.directionalEnabled", directionalEnabled)) {
        appDesc.automation.shadow.directionalEnabled = directionalEnabled;
    }
    if (bool pointLightEnabled = false; configManager.tryGet<bool>(AUTOMATION_CONFIG_DOC_NAME, "shadow.pointLightEnabled", pointLightEnabled)) {
        appDesc.automation.shadow.pointLightEnabled = pointLightEnabled;
    }
    if (bool pointLightUseIndirect = false; configManager.tryGet<bool>(AUTOMATION_CONFIG_DOC_NAME, "shadow.pointLightUseIndirect", pointLightUseIndirect)) {
        appDesc.automation.shadow.pointLightUseIndirect = pointLightUseIndirect;
    }
    if (bool pointLightIndirectCullEnabled = false; configManager.tryGet<bool>(AUTOMATION_CONFIG_DOC_NAME, "shadow.pointLightIndirectCullEnabled", pointLightIndirectCullEnabled)) {
        appDesc.automation.shadow.pointLightIndirectCullEnabled = pointLightIndirectCullEnabled;
    }
    if (uint32_t maxPointLightShadows = 0; configManager.tryGet<uint32_t>(AUTOMATION_CONFIG_DOC_NAME, "shadow.maxPointLightShadows", maxPointLightShadows)) {
        appDesc.automation.shadow.maxPointLightShadows = std::min(maxPointLightShadows, static_cast<uint32_t>(MAX_POINT_LIGHTS));
    }

    if (std::string filterText; configManager.tryGet<std::string>(AUTOMATION_CONFIG_DOC_NAME, "shadow.filter", filterText)) {
        EShadowFilter::T filter = EShadowFilter::Hard;
        if (tryParseShadowFilterValue(filterText, filter)) {
            appDesc.automation.shadow.filter = filter;
        }
        else {
            YA_CORE_WARN("Ignoring invalid automation shadow filter override: {}", filterText);
        }
    }
    else if (uint32_t filterValue = 0; configManager.tryGet<uint32_t>(AUTOMATION_CONFIG_DOC_NAME, "shadow.filter", filterValue)) {
        if (filterValue <= static_cast<uint32_t>(EShadowFilter::PCF_High)) {
            appDesc.automation.shadow.filter = static_cast<EShadowFilter::T>(filterValue);
        }
        else {
            YA_CORE_WARN("Ignoring invalid automation shadow filter override value: {}", filterValue);
        }
    }

    if (float bias = 0.0f; configManager.tryGet<float>(AUTOMATION_CONFIG_DOC_NAME, "shadow.bias", bias)) {
        appDesc.automation.shadow.bias = bias;
    }
    if (float normalBias = 0.0f; configManager.tryGet<float>(AUTOMATION_CONFIG_DOC_NAME, "shadow.normalBias", normalBias)) {
        appDesc.automation.shadow.normalBias = normalBias;
    }
    if (float directionalDistance = 0.0f; configManager.tryGet<float>(AUTOMATION_CONFIG_DOC_NAME, "shadow.directionalDistance", directionalDistance)) {
        appDesc.automation.shadow.directionalDistance = directionalDistance;
    }
}

void applyShadowAutomationOverrides(const AppAutomationShadowOverrides& overrides, ShadowSettings& shadowSettings)
{
    if (overrides.quality) {
        shadowSettings.applyQualityPreset(*overrides.quality);
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
    if (overrides.pointLightIndirectCullEnabled) {
        shadowSettings.pointLightIndirectCullEnabled = *overrides.pointLightIndirectCullEnabled;
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
    const auto frameServices = renderRuntime->buildFrameServices();

    if (AppAutomation::isRenderDocCapturePending(frameServices)) {
        return true;
    }

    if (AppAutomation::isRenderDocCaptureTerminal(frameServices)) {
        return false;
    }

    if (!bStableFrameReady) {
        return true;
    }

    const bool bRequested = AppAutomation::requestRenderDocCapture(frameServices);
    if (bRequested) {
        const uint64_t settleFrames = automation.screenshotSettleFrames > 0 ? automation.screenshotSettleFrames : 1;
        YA_CORE_INFO("Automation requested a single RenderDoc capture after {} warmup frames and {} stable frames",
                     automation.screenshotWarmupFrames,
                     settleFrames);
    }

    return AppAutomation::isRenderDocCapturePending(frameServices);
}

bool hasPendingAutomationWork(const App& app)
{
    auto&                       runtimeState = getAutomationRuntimeState();
    const AppAutomationOptions& automation   = app.getDesc().automation;

    const bool bScreenshotPending = hasScreenshotAutomation(automation) && !isScreenshotTerminal(runtimeState);

    bool bRenderDocPending = false;
    if (hasRenderDocAutomation(automation)) {
        if (const RenderRuntime* renderRuntime = app.getRenderRuntime()) {
            const auto frameServices = renderRuntime->buildFrameServices();
            bRenderDocPending = !AppAutomation::isRenderDocCaptureTerminal(frameServices);
        }
    }

    return bScreenshotPending || bRenderDocPending;
}

bool hasFrameAutomationConfig(const AppAutomationOptions& automation)
{
    return automation.exitAfterFrame > 0 ||
           hasScreenshotAutomation(automation) ||
           hasRenderDocAutomation(automation);
}
} // namespace

bool AppAutomation::isFrameAutomationEnabled(const App& app)
{
    return hasFrameAutomationConfig(app.getDesc().automation);
}

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

void AppAutomation::loadConfig(AppDesc& appDesc)
{
    auto& configManager = ConfigManager::get();
    configManager.openDocument(
        AUTOMATION_CONFIG_DOC_NAME,
        AUTOMATION_CONFIG_PATH,
        Config::OpenDocumentOptions{
            .bPersistIfMissing = true,
            .bReadOnly         = false,
        });

    if (appDesc.automation.configPath && !appDesc.automation.configPath->empty() &&
        *appDesc.automation.configPath != AUTOMATION_CONFIG_PATH) {
        YA_CORE_WARN("Ignoring legacy automation-config override '{}'; use {} for automation mode", *appDesc.automation.configPath, AUTOMATION_CONFIG_PATH);
    }
}

void AppAutomation::applyStartupOverrides(AppDesc& appDesc)
{
    getAutomationRuntimeState() = {};
    loadScreenshotAutomationOverrides(appDesc);
    loadRenderDocAutomationOverrides(appDesc);
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

bool AppAutomation::requestRenderDocCapture(const RenderRuntimeFrameServices& services)
{
    return services.requestAutomationRenderDocCapture ? services.requestAutomationRenderDocCapture() : false;
}

bool AppAutomation::isRenderDocCapturePending(const RenderRuntimeFrameServices& services)
{
    return services.isAutomationRenderDocCapturePending ? services.isAutomationRenderDocCapturePending() : false;
}

bool AppAutomation::isRenderDocCaptureTerminal(const RenderRuntimeFrameServices& services)
{
    return services.isAutomationRenderDocCaptureTerminal ? services.isAutomationRenderDocCaptureTerminal() : true;
}

const std::string& AppAutomation::getRenderDocCapturePath(const RenderRuntimeFrameServices& services)
{
    static const std::string emptyPath;
    return services.getAutomationRenderDocCapturePath ? services.getAutomationRenderDocCapturePath() : emptyPath;
}

const std::string& AppAutomation::getRenderDocPassSummaryPath(const RenderRuntimeFrameServices& services)
{
    static const std::string emptyPath;
    return services.getAutomationRenderDocPassSummaryPath ? services.getAutomationRenderDocPassSummaryPath() : emptyPath;
}

void AppAutomation::onFrameCompleted(App& app)
{
    YA_PROFILE_FUNCTION()

    auto&      runtimeState       = getAutomationRuntimeState();
    bool       bStableFrameReady  = false;
    bool       bScreenshotPending = false;
    bool       bRenderDocPending  = false;

    {
        YA_PROFILE_SCOPE("Automation/Stability");
        bStableFrameReady = isAutomationStableFrameReady(app);
    }
    {
        YA_PROFILE_SCOPE("Automation/Screenshot");
        bScreenshotPending = handleScreenshotAutomation(app, bStableFrameReady);
    }
    {
        YA_PROFILE_SCOPE("Automation/RenderDoc");
        bRenderDocPending = handleRenderDocAutomation(app, bStableFrameReady);
    }
    const bool bAutomationPending = bScreenshotPending || bRenderDocPending;

    if (RenderRuntime* renderRuntime = app.getRenderRuntime()) {
        YA_PROFILE_SCOPE("Automation/UpdateArtifacts");
        const auto frameServices = renderRuntime->buildFrameServices();
        profiling::setGpuCapturePath(AppAutomation::getRenderDocCapturePath(frameServices));
        profiling::setPassSummaryPath(AppAutomation::getRenderDocPassSummaryPath(frameServices));
        profiling::setScreenshotPath(runtimeState.screenshot.outputPath);
    }
    if (runtimeState.bQuitDeferred && !bAutomationPending) {
        runtimeState.bQuitDeferred = false;
        YA_CORE_INFO("Continuing deferred shutdown after automation work finished");
        app.requestQuit();
        return;
    }

    {
        YA_PROFILE_SCOPE("Automation/FlushArtifacts");
        profiling::flushRuntimeArtifacts();
    }
    if (bAutomationPending || !shouldRequestQuitAfterFrame(app)) {
        return;
    }

    YA_CORE_INFO("Automation requested graceful shutdown after frame {}", app.getDesc().automation.exitAfterFrame);
    app.requestQuit();
}

} // namespace ya
