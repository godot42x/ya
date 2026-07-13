#include "Runtime/App/Lifecycle/AppAutomation.h"

#include "Runtime/App/App.h"
#include "Runtime/App/Common/Shadow/Common/ShadowSettingsConfig.h"
#include "Runtime/App/RenderRuntime.h"
#include "Runtime/App/Utility/AppScreenshotCapture.h"
#include "Runtime/App/Utility/OffscreenJobRunner.h"

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
    bool                      bViewportResizeApplied = false;
    bool                      bPipelineSwitchApplied = false;
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

void loadViewportResizeAutomationOverrides(AppDesc& appDesc)
{
    auto& configManager = ConfigManager::get();
    if (!configManager.hasDocument(AUTOMATION_CONFIG_DOC_NAME)) {
        return;
    }

    uint32_t width  = 0;
    uint32_t height = 0;
    const bool bHasWidth  = configManager.tryGet<uint32_t>(AUTOMATION_CONFIG_DOC_NAME, "smoke.viewportResize.width", width);
    const bool bHasHeight = configManager.tryGet<uint32_t>(AUTOMATION_CONFIG_DOC_NAME, "smoke.viewportResize.height", height);
    if (!bHasWidth && !bHasHeight) {
        return;
    }

    if (width == 0 || height == 0) {
        YA_CORE_WARN("Ignoring invalid automation viewport resize override: {}x{}", width, height);
        return;
    }

    AppAutomationViewportResize resize{
        .width  = width,
        .height = height,
    };
    configManager.tryGet<uint64_t>(AUTOMATION_CONFIG_DOC_NAME, "smoke.viewportResize.frame", resize.frameIndex);
    appDesc.automation.viewportResize = resize;
}

void loadPipelineSwitchAutomationOverrides(AppDesc& appDesc)
{
    auto& configManager = ConfigManager::get();
    if (!configManager.hasDocument(AUTOMATION_CONFIG_DOC_NAME)) {
        return;
    }

    std::string pipelineText;
    if (!configManager.tryGet<std::string>(AUTOMATION_CONFIG_DOC_NAME, "smoke.renderPipeline.target", pipelineText) ||
        pipelineText.empty()) {
        return;
    }

    EAutomationRenderPipeline target = EAutomationRenderPipeline::Deferred;
    if (!tryParseAutomationRenderPipeline(pipelineText, target)) {
        YA_CORE_WARN("Ignoring invalid automation render pipeline override: {}", pipelineText);
        return;
    }

    AppAutomationPipelineSwitch pipelineSwitch{
        .target = target,
    };
    configManager.tryGet<uint64_t>(AUTOMATION_CONFIG_DOC_NAME, "smoke.renderPipeline.frame", pipelineSwitch.frameIndex);
    appDesc.automation.pipelineSwitch = pipelineSwitch;
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

bool handleScreenshotAutomation(App& app, const AppAutomationFrameContext& frameContext, bool bStableFrameReady)
{
    auto& runtimeState = getAutomationRuntimeState();

    const AppAutomationOptions& automation = app.getDesc().automation;
    if (!hasScreenshotAutomation(automation)) {
        return false;
    }

    AppScreenshotCapture::tryFinalize(frameContext.frameIndex, runtimeState.screenshot);
    if (runtimeState.bScreenshotRequested || isScreenshotTerminal(runtimeState)) {
        return !isScreenshotTerminal(runtimeState);
    }

    if (!bStableFrameReady) {
        return true;
    }

    if (!frameContext.render) {
        return false;
    }

    runtimeState.bScreenshotRequested = AppScreenshotCapture::request(frameContext.render,
                                                                      AppAutomation::buildOffscreenJobQueueService(app),
                                                                      frameContext.postprocessImage,
                                                                      frameContext.viewportTexture,
                                                                      frameContext.presentationTexture,
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

bool handleRenderDocAutomation(const AppAutomationOptions& automation,
                               const AppAutomationFrameContext& frameContext,
                               bool bStableFrameReady)
{
    if (!frameContext.isRenderDocCapturePending ||
        !frameContext.isRenderDocCaptureTerminal ||
        !frameContext.requestRenderDocCapture) {
        return false;
    }

    if (!hasRenderDocAutomation(automation)) {
        return false;
    }

    if (frameContext.isRenderDocCapturePending()) {
        return true;
    }

    if (frameContext.isRenderDocCaptureTerminal()) {
        return false;
    }

    if (!bStableFrameReady) {
        return true;
    }

    const bool bRequested = frameContext.requestRenderDocCapture();
    if (bRequested) {
        const uint64_t settleFrames = automation.screenshotSettleFrames > 0 ? automation.screenshotSettleFrames : 1;
        YA_CORE_INFO("Automation requested a single RenderDoc capture after {} warmup frames and {} stable frames",
                     automation.screenshotWarmupFrames,
                     settleFrames);
    }

    return frameContext.isRenderDocCapturePending();
}

const std::string& getAutomationCapturePathFallback()
{
    static const std::string emptyPath;
    return emptyPath;
}

std::string getAutomationCapturePath(const std::function<const std::string&()>& pathProvider)
{
    return pathProvider ? pathProvider() : getAutomationCapturePathFallback();
}

bool hasPendingAutomationWork(const App& app, const AppAutomationFrameContext* frameContext = nullptr)
{
    auto&                       runtimeState = getAutomationRuntimeState();
    const AppAutomationOptions& automation   = app.getDesc().automation;

    const bool bScreenshotPending = hasScreenshotAutomation(automation) && !isScreenshotTerminal(runtimeState);

    bool bRenderDocPending = false;
    if (hasRenderDocAutomation(automation)) {
        if (frameContext && frameContext->isRenderDocCaptureTerminal) {
            bRenderDocPending = !frameContext->isRenderDocCaptureTerminal();
        }
        else if (const RenderRuntime* renderRuntime = app.getRenderRuntime()) {
            bRenderDocPending = !renderRuntime->getDiagnosticsService().isAutomationRenderDocCaptureTerminal();
        }
    }

    return bScreenshotPending || bRenderDocPending;
}

bool hasFrameAutomationConfig(const AppAutomationOptions& automation)
{
    return automation.exitAfterFrame > 0 ||
           automation.viewportResize.has_value() ||
           automation.pipelineSwitch.has_value() ||
           hasScreenshotAutomation(automation) ||
           hasRenderDocAutomation(automation);
}

RenderRuntime::ERenderPipeline toRuntimeRenderPipeline(EAutomationRenderPipeline pipeline)
{
    switch (pipeline) {
    case EAutomationRenderPipeline::Forward:
        return RenderRuntime::ERenderPipeline::Forward;
    case EAutomationRenderPipeline::Deferred:
        return RenderRuntime::ERenderPipeline::Deferred;
    }
    return RenderRuntime::ERenderPipeline::Deferred;
}

void applyScheduledSmokeActions(App& app, uint64_t frameIndex)
{
    auto&                       runtimeState = getAutomationRuntimeState();
    const AppAutomationOptions& automation   = app.getDesc().automation;
    auto*                       renderRuntime = app.getRenderRuntime();
    if (!renderRuntime) {
        return;
    }

    if (automation.pipelineSwitch &&
        !runtimeState.bPipelineSwitchApplied &&
        frameIndex >= automation.pipelineSwitch->frameIndex) {
        renderRuntime->setPendingRenderPipeline(toRuntimeRenderPipeline(automation.pipelineSwitch->target));
        runtimeState.bPipelineSwitchApplied = true;
        YA_CORE_INFO("Automation queued render pipeline switch to {} at frame {}",
                     automation.pipelineSwitch->target == EAutomationRenderPipeline::Forward ? "Forward" : "Deferred",
                     frameIndex);
    }

    if (automation.viewportResize &&
        !runtimeState.bViewportResizeApplied &&
        frameIndex >= automation.viewportResize->frameIndex) {
        Rect2D resizeRect = renderRuntime->getViewportRect();
        resizeRect.extent = glm::vec2(static_cast<float>(automation.viewportResize->width),
                                      static_cast<float>(automation.viewportResize->height));

        if (app._editorLayer) {
            app._editorLayer->queueViewportResize(resizeRect);
        }
        else {
            renderRuntime->onViewportResized(resizeRect);
            if (resizeRect.extent.y > 0.0f) {
                app.camera.setAspectRatio(resizeRect.extent.x / resizeRect.extent.y);
            }
        }

        runtimeState.bViewportResizeApplied = true;
        YA_CORE_INFO("Automation queued viewport resize to {}x{} at frame {}",
                     automation.viewportResize->width,
                     automation.viewportResize->height,
                     frameIndex);
    }
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
    const std::string automationConfigPath = appDesc.automation.configPath && !appDesc.automation.configPath->empty()
                                               ? *appDesc.automation.configPath
                                               : AUTOMATION_CONFIG_PATH;

    auto& configManager = ConfigManager::get();
    configManager.openDocument(
        AUTOMATION_CONFIG_DOC_NAME,
        automationConfigPath,
        Config::OpenDocumentOptions{
            .bPersistIfMissing = true,
            .bReadOnly         = false,
        });
}

void AppAutomation::applyStartupOverrides(AppDesc& appDesc)
{
    getAutomationRuntimeState() = {};
    loadScreenshotAutomationOverrides(appDesc);
    loadRenderDocAutomationOverrides(appDesc);
    loadViewportResizeAutomationOverrides(appDesc);
    loadPipelineSwitchAutomationOverrides(appDesc);
    shadow_settings::loadAutomationOverridesFromConfig(appDesc.automation.shadow);
    if (appDesc.automation.renderDocCapture) {
        appDesc.bEnableRenderDoc = true;
    }
    if (appDesc.automation.scenePath) {
        appDesc.defaultScenePath = appDesc.automation.scenePath;
    }
}

void AppAutomation::applyRuntimeOverrides(App& app)
{
    shadow_settings::applyAutomationOverrides(app.getDesc().automation.shadow, app.getShadowSettings());
}

void AppAutomation::recordPresentationCapture(Texture* presentationSourceTexture,
                                              uint64_t frameIndex,
                                              ICommandBuffer* cmdBuf)
{
    auto& runtimeState = getAutomationRuntimeState();
    AppScreenshotCapture::recordPresentationCapture(presentationSourceTexture, frameIndex, runtimeState.screenshot, cmdBuf);
}

OffscreenJobQueueService AppAutomation::buildOffscreenJobQueueService(App& app)
{
    OffscreenJobQueueService queueService{};
    queueService.enqueue = [&app](const std::shared_ptr<OffscreenJobState>& job, std::function<void(ICommandBuffer*)> task)
    {
        app.taskManager.enqueueOffscreenTask(job, std::move(task));
    };
    return queueService;
}

void AppAutomation::onFrameCompleted(App& app, const AppAutomationFrameContext& frameContext)
{
    YA_PROFILE_FUNCTION()

    {
        YA_PROFILE_SCOPE("Automation/SmokeActions");
        applyScheduledSmokeActions(app, frameContext.frameIndex);
    }

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
        bScreenshotPending = handleScreenshotAutomation(app, frameContext, bStableFrameReady);
    }
    {
        YA_PROFILE_SCOPE("Automation/RenderDoc");
        bRenderDocPending = handleRenderDocAutomation(app.getDesc().automation, frameContext, bStableFrameReady);
    }
    const bool bAutomationPending = bScreenshotPending || bRenderDocPending;

    if (frameContext.getRenderDocCapturePath && frameContext.getRenderDocPassSummaryPath) {
        YA_PROFILE_SCOPE("Automation/UpdateArtifacts");
        profiling::setGpuCapturePath(getAutomationCapturePath(frameContext.getRenderDocCapturePath));
        profiling::setPassSummaryPath(getAutomationCapturePath(frameContext.getRenderDocPassSummaryPath));
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
