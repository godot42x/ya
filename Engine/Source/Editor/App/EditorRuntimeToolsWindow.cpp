#include "Editor/App/EditorRuntimeToolsWindow.h"

#include "Config/ConfigManager.h"
#include "Core/Camera/FreeCameraController.h"
#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Core/Profiling/Profiling.h"
#include "Editor/App/EditorLayer.h"
#include "Editor/EditorProfilingSettings.h"
#include "Editor/Panels/RenderTargetInspector.h"
#include "Runtime/App/App.h"
#include "Runtime/App/Common/PostProcessingStage.h"
#include "Runtime/App/Common/Shadow/BasicShadowMap/BasicShadowMapTechnique.h"
#include "Runtime/App/Common/Shadow/ShadowStage.h"
#include "Runtime/App/DebugRenderSystem.h"
#include "Runtime/App/DeferredRender/DeferredRenderPipeline.h"
#include "Runtime/App/ForwardRender/ForwardRenderPipeline.h"
#include "Runtime/App/RenderRuntime.h"
#include "Runtime/App/Utility/FPSCtrl.h"
#include "Runtime/App/Common/Shadow/Common/ShadowSettingsConfig.h"
#include "Render/2D/Render2D.h"
#include "Render/Core/Swapchain.h"

#include <algorithm>
#include <array>
#include <glm/gtc/type_ptr.hpp>

namespace ya
{

extern ClearValue colorClearValue;
extern ClearValue depthClearValue;

namespace
{

constexpr const char* kCullModeLabels       = "None\0Front\0Back\0FrontAndBack\0";
constexpr const char* kPolygonModeLabels    = "Fill\0Line\0Point\0";
constexpr const char* kCompareOpLabels      = "Never\0Less\0Equal\0LessOrEqual\0Greater\0NotEqual\0GreaterOrEqual\0Always\0";
constexpr const char* kPresentModeLabels    = "Immediate\0Mailbox\0FIFO\0FIFO Relaxed\0";
constexpr const char* kRenderPipelineLabels = "Forward\0Deferred\0";

DeferredRenderPipeline* getDeferredPipeline(App& app)
{
    auto* renderRuntime = app.getRenderRuntime();
    return renderRuntime ? dynamic_cast<DeferredRenderPipeline*>(renderRuntime->getActivePipeline()) : nullptr;
}

ForwardRenderPipeline* getForwardPipeline(App& app)
{
    auto* renderRuntime = app.getRenderRuntime();
    return renderRuntime ? dynamic_cast<ForwardRenderPipeline*>(renderRuntime->getActivePipeline()) : nullptr;
}

template <typename Fn>
void renderPerfTree(const char* label, float valueMs, Fn&& body)
{
    if (!ImGui::TreeNode(label, "%s  %.3f ms", label, valueMs)) {
        return;
    }
    body();
    ImGui::TreePop();
}

void renderPerfLeaf(const char* label, float valueMs, float parentMs = -1.0f)
{
    if (parentMs > 0.0f) {
        ImGui::Text("%s: %.3f ms (%.1f%%)", label, valueMs, valueMs / parentMs * 100.0f);
        return;
    }
    ImGui::Text("%s: %.3f ms", label, valueMs);
}

void renderFrameStatsContent(const App& app, float dt)
{
    static constexpr size_t kHistorySize = 120;
    static std::array<float, kHistorySize> fpsHistory{};
    static size_t historyHead = 0;
    static size_t historyFill = 0;
    static float  fpsSum      = 0.0f;

    const float fps = dt > 0.0f ? 1.0f / dt : 0.0f;
    if (historyFill >= kHistorySize) {
        fpsSum -= fpsHistory[historyHead];
    }
    fpsHistory[historyHead] = fps;
    fpsSum += fps;
    historyHead = (historyHead + 1) % kHistorySize;
    historyFill = std::min(historyFill + 1, kHistorySize);

    const float avgFps = historyFill > 0 ? fpsSum / static_cast<float>(historyFill) : 0.0f;

    ImGui::Text("Frame Index: %llu", static_cast<unsigned long long>(app.getFrameIndex()));
    ImGui::Text("Delta: %.2f ms", dt * 1000.0f);
    ImGui::Text("FPS: %.1f", fps);
    ImGui::Text("Avg FPS: %.1f", avgFps);
}

void savePostProcessingSettings(const PostProcessingState& settings)
{
    ConfigManager::Editor("runtime")
        .set("render.postprocess.basic.inversion", settings.bEnableInversion)
        .set("render.postprocess.basic.grayscale", static_cast<int>(settings.grayscaleMode))
        .set("render.postprocess.basic.kernel", static_cast<int>(settings.kernelMode))
        .set("render.postprocess.basic.kernelTexelOffset", settings.kernelTexelOffset)
        .set("render.postprocess.basic.tonemapping.enabled", settings.bEnableToneMapping)
        .set("render.postprocess.basic.tonemapping.curve", static_cast<int>(settings.toneMappingCurve))
        .set("render.postprocess.basic.tonemapping.exposure", settings.exposure)
        .set("render.postprocess.basic.output.gammaCorrection", settings.bEnableGammaCorrection)
        .set("render.postprocess.basic.output.gamma", settings.gamma)
        .set("render.postprocess.basic.output.randomGrain", settings.bEnableRandomGrain)
        .set("render.postprocess.basic.output.randomGrainStrength", settings.randomGrainStrength)
        .set("render.postprocess.bloom.enabled", settings.bEnableBloom)
        .set("render.postprocess.bloom.threshold", settings.bloomThreshold)
        .set("render.postprocess.bloom.softKnee", settings.bloomSoftKnee)
        .set("render.postprocess.bloom.extractIntensity", settings.bloomExtractIntensity)
        .set("render.postprocess.bloom.blurPasses", static_cast<int>(settings.bloomBlurPasses))
        .set("render.postprocess.bloom.strength", settings.bloomStrength);
}

template <typename T>
void migrateLegacyRuntimeSetting(std::string_view key)
{
    auto& config = ConfigManager::get();
    if (config.hasValue("runtime", key)) {
        return;
    }

    T value{};
    if (config.tryGet<T>("editor", key, value)) {
        ConfigManager::Editor("runtime").set(key, value);
    }
}

void saveShadowSettings(const ShadowSettings& settings)
{
    shadow_settings::saveRuntimeSettings(settings);
}

void saveDeferredSSAOSettings(const SSAOStage& stage)
{
    ConfigManager::Editor("runtime")
        .set("render.deferred.ssao.radius", stage.getRadius())
        .set("render.deferred.ssao.bias", stage.getBias())
        .set("render.deferred.ssao.power", stage.getPower())
        .set("render.deferred.ssao.intensity", stage.getIntensity());
}

void saveDeferredLightSettings(const LightStage& stage)
{
    ConfigManager::Editor("runtime")
        .set("render.deferred.light.enablePBRDiffuseIBL", stage.isPBRDiffuseIBLEnabled())
        .set("render.deferred.light.enablePBRSpecularIBL", stage.isPBRSpecularIBLEnabled());
}

void renderGraphicsPipelineInspector(const char* label, IGraphicsPipeline* pipeline)
{
    if (!pipeline) {
        ImGui::TextDisabled("%s unavailable", label);
        return;
    }

    ImGui::PushID(pipeline);
    if (!ImGui::TreeNode(label)) {
        ImGui::PopID();
        return;
    }

    auto ci = pipeline->getDesc();
    bool bDirty = false;

    ImGui::TextWrapped("Shader: %s", ci.shaderDesc.shaderName.c_str());
    ImGui::Text("Dirty: %s", pipeline->isDirty() ? "Yes" : "No");
    if (ImGui::Button("Reload Shaders")) {
        pipeline->markDirty();
    }

    int cullMode = static_cast<int>(ci.rasterizationState.cullMode);
    if (ImGui::Combo("Cull Mode", &cullMode, kCullModeLabels)) {
        pipeline->setCullMode(static_cast<ECullMode::T>(cullMode));
    }

    int polygonMode = static_cast<int>(ci.rasterizationState.polygonMode);
    if (ImGui::Combo("Polygon Mode", &polygonMode, kPolygonModeLabels)) {
        pipeline->setPolygonMode(static_cast<EPolygonMode::T>(polygonMode));
    }

    bDirty |= ImGui::Checkbox("Depth Test Enable", &ci.depthStencilState.bDepthTestEnable);
    bDirty |= ImGui::Checkbox("Depth Write Enable", &ci.depthStencilState.bDepthWriteEnable);

    int compareOp = static_cast<int>(ci.depthStencilState.depthCompareOp);
    if (ImGui::Combo("Depth Compare", &compareOp, kCompareOpLabels)) {
        pipeline->setDepthCompareOp(static_cast<ECompareOp::T>(compareOp));
    }

    bool bDepthBiasEnable = ci.rasterizationState.bDepthBiasEnable;
    if (ImGui::Checkbox("Depth Bias Enable", &bDepthBiasEnable)) {
        pipeline->setDepthBiasEnable(bDepthBiasEnable);
    }
    if (bDepthBiasEnable) {
        float constantFactor = ci.rasterizationState.depthBiasConstantFactor;
        float clamp          = ci.rasterizationState.depthBiasClamp;
        float slopeFactor    = ci.rasterizationState.depthBiasSlopeFactor;
        bool  bBiasChanged   = false;
        bBiasChanged |= ImGui::DragFloat("Depth Bias Constant", &constantFactor, 0.1f, -10.0f, 10.0f, "%.2f");
        bBiasChanged |= ImGui::DragFloat("Depth Bias Clamp", &clamp, 0.001f, -1.0f, 1.0f, "%.4f");
        bBiasChanged |= ImGui::DragFloat("Depth Bias Slope", &slopeFactor, 0.1f, -10.0f, 10.0f, "%.2f");
        if (bBiasChanged) {
            pipeline->setDepthBias(constantFactor, clamp, slopeFactor);
        }
    }

    if (!ci.shaderDesc.defines.empty() && ImGui::TreeNode("Shader Defines")) {
        for (const auto& define : ci.shaderDesc.defines) {
            ImGui::BulletText("%s", define.c_str());
        }
        ImGui::TreePop();
    }

    if (bDirty) {
        pipeline->updateDesc(std::move(ci));
    }

    ImGui::TreePop();
    ImGui::PopID();
}

bool renderShadowSettingsControls(ShadowSettings& shadowSettings, bool bAllowDirectionalControls)
{
    bool bDirty         = false;
    bool bShadowEnabled = shadowSettings.isEnabled();
    if (ImGui::Checkbox("Enable Shadow Mapping", &bShadowEnabled)) {
        if (bShadowEnabled) {
            if (shadowSettings.quality == EShadowQuality::Off) {
                shadowSettings.applyQualityPreset(EShadowQuality::Medium);
            }
        }
        else {
            shadowSettings.quality = EShadowQuality::Off;
        }
        bDirty = true;
    }

    if (!shadowSettings.isEnabled()) {
        return bDirty;
    }

    static const char* qualityNames[] = {"Low", "Medium", "High", "Ultra"};
    int qualityIdx = std::max(0, static_cast<int>(shadowSettings.quality) - 1);
    if (ImGui::Combo("Quality Preset", &qualityIdx, qualityNames, IM_ARRAYSIZE(qualityNames))) {
        shadowSettings.applyQualityPreset(static_cast<EShadowQuality::T>(qualityIdx + 1));
        bDirty = true;
    }

    if (bAllowDirectionalControls) {
        bDirty |= ImGui::Checkbox("Directional Shadow", &shadowSettings.directionalEnabled);
    }
    bDirty |= ImGui::Checkbox("Point Light Shadow", &shadowSettings.pointLightEnabled);
    bDirty |= ImGui::Checkbox("Point Light Indirect Draw", &shadowSettings.pointLightUseIndirect);
    bDirty |= ImGui::Checkbox("Point Light Indirect Cull", &shadowSettings.pointLightIndirectCullEnabled);

    int maxPointLights = static_cast<int>(shadowSettings.maxPointLightShadows);
    if (ImGui::SliderInt("Max Point Shadows", &maxPointLights, 0, MAX_POINT_LIGHTS)) {
        shadowSettings.maxPointLightShadows = static_cast<uint32_t>(maxPointLights);
        bDirty = true;
    }

    int shadowResolution = static_cast<int>(shadowSettings.resolution);
    if (ImGui::DragInt("Shadow Resolution", &shadowResolution, 16.0f, 128, 8192, "%d")) {
        shadowSettings.resolution = static_cast<uint32_t>(std::clamp(shadowResolution, 128, 8192));
        bDirty = true;
    }

    bDirty |= ImGui::DragFloat("Depth Bias", &shadowSettings.bias, 0.0001f, 0.0f, 0.1f, "%.5f");
    bDirty |= ImGui::DragFloat("Normal Bias", &shadowSettings.normalBias, 0.0001f, 0.0f, 0.1f, "%.5f");

    if (bAllowDirectionalControls) {
        bDirty |= ImGui::DragFloat("Directional Distance", &shadowSettings.directionalDistance, 0.5f, 1.0f, 500.0f, "%.1f");
        bDirty |= ImGui::Checkbox("Stable Directional Fit", &shadowSettings.directionalStableFit);
        int directionalCascades = static_cast<int>(shadowSettings.directionalCascades);
        if (ImGui::SliderInt("Directional Cascades", &directionalCascades, 0, 4)) {
            shadowSettings.directionalCascades = static_cast<uint32_t>(directionalCascades);
            bDirty = true;
        }
    }

    static const char* filterNames[] = {"Hard", "PCF Low", "PCF High"};
    int filter = static_cast<int>(shadowSettings.filter);
    if (ImGui::Combo("Shadow Filter", &filter, filterNames, IM_ARRAYSIZE(filterNames))) {
        shadowSettings.filter = static_cast<EShadowFilter::T>(filter);
        bDirty = true;
    }

    return bDirty;
}

bool renderPostProcessingSettings(PostProcessingState& post)
{
    bool changed = false;
    changed |= ImGui::Checkbox("Inversion", &post.bEnableInversion);

    int grayscaleMode = static_cast<int>(post.grayscaleMode);
    if (ImGui::Combo("Grayscale", &grayscaleMode, "None\0Average\0Weighted\0")) {
        post.grayscaleMode = static_cast<PostProcessingState::EGrayscaleMode>(grayscaleMode);
        changed = true;
    }

    int kernelMode = static_cast<int>(post.kernelMode);
    if (ImGui::Combo("Kernel", &kernelMode, "None\0Sharpen\0Blur\0Edge Detection\0")) {
        post.kernelMode = static_cast<PostProcessingState::EKernelMode>(kernelMode);
        changed = true;
    }
    ImGui::BeginDisabled(post.kernelMode == PostProcessingState::EKernelMode::None);
    changed |= ImGui::DragFloat("Kernel Texel Offset", &post.kernelTexelOffset, 0.0001f, 0.0001f, 0.02f, "%.5f");
    ImGui::EndDisabled();

    changed |= ImGui::Checkbox("Enable Tone Mapping", &post.bEnableToneMapping);
    ImGui::BeginDisabled(!post.bEnableToneMapping);
    int toneMappingCurve = static_cast<int>(post.toneMappingCurve);
    if (ImGui::Combo("Tone Mapping Curve", &toneMappingCurve, "ACES\0Uncharted2\0")) {
        post.toneMappingCurve = static_cast<PostProcessingState::EToneMappingCurve>(toneMappingCurve);
        changed = true;
    }
    changed |= ImGui::DragFloat("Exposure", &post.exposure, 0.01f, 0.0f, 8.0f, "%.2f");
    ImGui::EndDisabled();

    changed |= ImGui::Checkbox("Gamma Correction", &post.bEnableGammaCorrection);
    ImGui::BeginDisabled(!post.bEnableGammaCorrection);
    changed |= ImGui::DragFloat("Gamma", &post.gamma, 0.01f, 0.1f, 4.0f);
    ImGui::EndDisabled();

    changed |= ImGui::Checkbox("Random Grain", &post.bEnableRandomGrain);
    ImGui::BeginDisabled(!post.bEnableRandomGrain);
    changed |= ImGui::DragFloat("Grain Strength", &post.randomGrainStrength, 0.001f, 0.0f, 0.25f, "%.3f");
    ImGui::EndDisabled();

    changed |= ImGui::Checkbox("Enable Bloom", &post.bEnableBloom);
    ImGui::BeginDisabled(!post.bEnableBloom);
    changed |= ImGui::DragFloat("Bloom Threshold", &post.bloomThreshold, 0.01f, 0.0f, 16.0f, "%.2f");
    changed |= ImGui::DragFloat("Bloom Soft Knee", &post.bloomSoftKnee, 0.01f, 0.0f, 2.0f, "%.2f");
    changed |= ImGui::DragFloat("Bloom Extract Intensity", &post.bloomExtractIntensity, 0.05f, 0.0f, 8.0f, "%.2f");
    int bloomBlurPasses = static_cast<int>(post.bloomBlurPasses);
    if (ImGui::DragInt("Bloom Blur Passes", &bloomBlurPasses, 1.0f, 1, 12)) {
        post.bloomBlurPasses = static_cast<uint32_t>(std::max(1, bloomBlurPasses));
        changed = true;
    }
    changed |= ImGui::DragFloat("Bloom Strength", &post.bloomStrength, 0.05f, 0.0f, 4.0f, "%.2f");
    ImGui::EndDisabled();

    return changed;
}

void renderPresentationSettings(App& app, RenderRuntime& runtime)
{
    int currentRenderPipeline = static_cast<int>(runtime.getPendingRenderPipeline());
    if (ImGui::Combo("Render Pipeline", &currentRenderPipeline, kRenderPipelineLabels)) {
        runtime.setPendingRenderPipeline(static_cast<RenderRuntime::ERenderPipeline>(currentRenderPipeline));
    }
    if (runtime.getPendingRenderPipeline() != runtime.getRenderPipeline()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "(switch pending)");
    }

    if (ImGui::Button("Reload Active Pipeline")) {
        runtime.requestActivePipelineReload();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("rebuilds shaders and pipeline resources");

    ImGui::DragFloat("Viewport Scale", &runtime._viewportFrameBufferScale, 0.1f, 1.0f, 10.0f);

    if (auto* render = app.getRender()) {
        if (auto* swapchain = render->getSwapchain()) {
            bool bVsync = swapchain->getVsync();
            if (ImGui::Checkbox("VSync", &bVsync)) {
                swapchain->setVsync(bVsync);
            }

            EPresentMode::T presentMode = swapchain->getPresentMode();
            if (ImGui::Combo("Present Mode", reinterpret_cast<int*>(&presentMode), kPresentModeLabels)) {
                app.taskManager.registerFrameTask([swapchain, presentMode]() {
                    swapchain->setPresentMode(presentMode);
                });
            }
        }
    }
}

void renderDeferredSettingsContent(App& app)
{
    auto* runtime = app.getRenderRuntime();
    auto* pipeline = getDeferredPipeline(app);
    if (!runtime || !pipeline) {
        ImGui::TextDisabled("Deferred-only settings are unavailable while the forward pipeline is active.");
        return;
    }

    auto settings = pipeline->buildSettingsSnapshot();
    bool changed = false;

    if (ImGui::TreeNode("General")) {
        changed |= ImGui::Checkbox("GBuffer Reverse Viewport Y", &settings.bReverseViewportY);
        ImGui::TextUnformatted("GBuffer ID + switch/case Light Pass");
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Lighting")) {
        if (pipeline->_lightStage) {
            bool diffuseIBL = pipeline->_lightStage->isPBRDiffuseIBLEnabled();
            bool specularIBL = pipeline->_lightStage->isPBRSpecularIBLEnabled();
            bool bLightChanged = false;
            bLightChanged |= ImGui::Checkbox("Enable PBR Diffuse IBL", &diffuseIBL);
            bLightChanged |= ImGui::Checkbox("Enable PBR Specular IBL", &specularIBL);
            if (bLightChanged) {
                pipeline->_lightStage->setIBLSettings(diffuseIBL, specularIBL);
                saveDeferredLightSettings(*pipeline->_lightStage);
            }
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Ambient Occlusion")) {
        changed |= ImGui::Checkbox("Enable SSAO", &settings.bSSAOEnabled);
        if (settings.bSSAOEnabled && pipeline->_ssaoStage) {
            float radius    = pipeline->_ssaoStage->getRadius();
            float bias      = pipeline->_ssaoStage->getBias();
            float power     = pipeline->_ssaoStage->getPower();
            float intensity = pipeline->_ssaoStage->getIntensity();
            bool reverseY   = pipeline->_ssaoStage->isReverseYEnabled();
            bool bSSAOChanged = false;
            bSSAOChanged |= ImGui::DragFloat("Radius", &radius, 0.01f, 0.05f, 5.0f, "%.3f");
            bSSAOChanged |= ImGui::DragFloat("Bias", &bias, 0.001f, 0.0f, 0.2f, "%.4f");
            bSSAOChanged |= ImGui::DragFloat("Power", &power, 0.01f, 0.1f, 4.0f, "%.3f");
            bSSAOChanged |= ImGui::DragFloat("Intensity", &intensity, 0.05f, 0.0f, 8.0f, "%.3f");
            if (bSSAOChanged) {
                pipeline->_ssaoStage->setSettings(radius, bias, power, intensity, reverseY);
                saveDeferredSSAOSettings(*pipeline->_ssaoStage);
            }
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Post Process")) {
        changed |= renderPostProcessingSettings(settings.postProcessing);
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Shadows")) {
        changed |= renderShadowSettingsControls(settings.shadow, true);
        ImGui::TreePop();
    }

    if (changed) {
        pipeline->requestSettings(settings);
        ConfigManager::Editor("runtime")
            .set("render.deferred.reverseViewportY", settings.bReverseViewportY)
            .set("render.deferred.ssaoEnabled", settings.bSSAOEnabled);
        savePostProcessingSettings(settings.postProcessing);
        saveShadowSettings(settings.shadow);
    }
}

void renderForwardSettingsContent(App& app)
{
    auto* pipeline = getForwardPipeline(app);
    if (!pipeline) {
        ImGui::TextDisabled("Forward-only settings are unavailable while the deferred pipeline is active.");
        return;
    }

    if (ImGui::TreeNode("Shadows")) {
        ShadowSettings shadowSettings = pipeline->getCurrentShadowSettings();
        if (renderShadowSettingsControls(shadowSettings, false)) {
            pipeline->requestShadowSettings(shadowSettings);
            saveShadowSettings(shadowSettings);
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Post Process")) {
        auto post = pipeline->_postProcessStage.getState();
        if (renderPostProcessingSettings(post)) {
            pipeline->_postProcessStage.getState() = post;
            savePostProcessingSettings(post);
        }
        ImGui::TreePop();
    }
}

void renderEditorCameraContent(EditorLayer& layer, FreeCameraController& controller)
{
    auto& camera   = layer.getCamera();
    auto  position = camera._position;
    auto  rotation = camera._rotation;
    bool  bChanged = false;

    bChanged |= ImGui::DragFloat3("Camera Position", glm::value_ptr(position), 0.01f, -100.0f, 100.0f);
    bChanged |= ImGui::DragFloat3("Camera Rotation", glm::value_ptr(rotation), 1.0f, -180.0f, 180.0f);
    ImGui::DragFloat("Move Speed", &controller._moveSpeed, 0.1f, 0.1f, 20.0f);
    ImGui::DragFloat("Rotation Speed", &controller._rotationSpeed, 1.0f, 10.0f, 180.0f);
    ImGui::TextDisabled("Hold right mouse button to rotate camera");
    ImGui::TextDisabled("WASD: Move horizontally, QE: Move vertically");

    if (bChanged) {
        camera.setPositionAndRotation(position, rotation);
    }
}

void renderClearValuesContent()
{
    float color[4] = {colorClearValue.color.r, colorClearValue.color.g, colorClearValue.color.b, colorClearValue.color.a};
    if (ImGui::ColorEdit4("Color Clear Value", color)) {
        colorClearValue = ClearValue(color[0], color[1], color[2], color[3]);
    }

    float depth = depthClearValue.depthStencil.depth;
    if (ImGui::DragFloat("Depth Clear Value", &depth, 0.01f, 0.0f, 1.0f)) {
        depthClearValue = ClearValue(depth, depthClearValue.depthStencil.stencil);
    }
}

void renderRender2DDebugContent()
{
    auto& data = Render2D::data;
    ImGui::Checkbox("Reverse Viewport Y", &data.bReverseViewport);

    int worldCullMode = static_cast<int>(data.worldCullMode);
    if (ImGui::Combo("World Cull Mode", &worldCullMode, kCullModeLabels)) {
        data.worldCullMode = static_cast<ECullMode::T>(worldCullMode);
    }

    int screenCullMode = static_cast<int>(data.screenCullMode);
    if (ImGui::Combo("Screen Cull Mode", &screenCullMode, kCullModeLabels)) {
        data.screenCullMode = static_cast<ECullMode::T>(screenCullMode);
    }

    ImGui::InputInt("Text Layout Mode", &data.TextLayoutMode);
    ImGui::TextDisabled("Viewport: %u x %u", data.windowWidth, data.windowHeight);
}

void renderSessionContent(App& app)
{
    AppMode mode = app._appMode;
    if (ImGui::Combo("App Mode", reinterpret_cast<int*>(&mode), "Control\0Drawing\0")) {
        app._appMode = mode;
    }

    if (auto* fpsCtrl = FPSControl::get()) {
        ImGui::SeparatorText("FPS Control");
        ImGui::Text("FPS Limit: %.1f", fpsCtrl->fpsLimit);

        static float newFpsLimit = fpsCtrl->fpsLimit;
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputFloat("New Limit", &newFpsLimit, 10.0f, 10.0f, "%.1f");
        ImGui::SameLine();
        if (ImGui::Button("Apply FPS Limit")) {
            fpsCtrl->setFPSLimit(newFpsLimit);
        }
        ImGui::Checkbox("Enable FPS Control", &fpsCtrl->bEnable);
    }
}

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

    const auto paths = profiling::getRuntimeSessionPaths();
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
    static constexpr size_t WINDOW_SIZES[] = {1, 10, 30, 60};
    static constexpr const char* WINDOW_LABELS[] = {"Last", "10 frames", "30 frames", "60 frames"};
    int currentWindowIndex = 0;
    for (int i = 0; i < IM_ARRAYSIZE(WINDOW_SIZES); ++i) {
        if (perfState.getAverageWindowSize() == WINDOW_SIZES[i]) {
            currentWindowIndex = i;
            break;
        }
    }
    if (ImGui::Combo("Metrics Average", &currentWindowIndex, WINDOW_LABELS, IM_ARRAYSIZE(WINDOW_LABELS))) {
        perfState.setAverageWindowSize(WINDOW_SIZES[currentWindowIndex]);
    }

    ImGui::Text("Frame CPU: %.3f ms", perfState.getDisplayValue(perf::sample::renderFrame(), perf::metric::cpuTimeMs()));
    ImGui::Text("Frame GPU: %.3f ms", perfState.getDisplayValue(perf::sample::renderFrame(), perf::metric::gpuTimeMs()));
}

void renderDiagnosticsContent(App& app)
{
    auto* runtime = app.getRenderRuntime();
    if (!runtime) {
        return;
    }

    auto& renderDoc = runtime->getDiagnosticsService().getRenderDocState();
    const bool bAvailable = renderDoc.capture && renderDoc.capture->isAvailable();
    ImGui::Text("RenderDoc: %s", bAvailable ? "Available" : "Unavailable");
    ImGui::TextWrapped("DLL Path: %s", renderDoc.configuredDllPath.empty() ? "<default>" : renderDoc.configuredDllPath.c_str());
    ImGui::TextWrapped("Output Dir: %s", renderDoc.configuredOutputDir.empty() ? "<default>" : renderDoc.configuredOutputDir.c_str());
    ImGui::TextWrapped("Last Capture: %s", renderDoc.lastCapturePath.empty() ? "<none>" : renderDoc.lastCapturePath.c_str());

    if (!bAvailable) {
        return;
    }

    bool bCaptureEnabled = renderDoc.capture->isCaptureEnabled();
    if (ImGui::Checkbox("Capture Enabled", &bCaptureEnabled)) {
        renderDoc.capture->setCaptureEnabled(bCaptureEnabled);
    }

    bool bHudVisible = renderDoc.capture->isHUDVisible();
    if (ImGui::Checkbox("Show RenderDoc HUD", &bHudVisible)) {
        renderDoc.capture->setHUDVisible(bHudVisible);
    }

    ImGui::Text("Capturing: %s", renderDoc.capture->isCapturing() ? "Yes" : "No");
    ImGui::Text("Delay Frames: %u", renderDoc.capture->getDelayFrames());
    ImGui::Combo("On Capture", &renderDoc.onCaptureAction, "None\0Open Replay UI\0Open Capture Folder\0");

    ImGui::BeginDisabled(!renderDoc.capture->isCaptureEnabled());
    if (ImGui::Button("Capture Next Frame (F9)")) {
        renderDoc.capture->requestNextFrame();
    }
    if (ImGui::Button("Capture After 120 Frames (Ctrl+F9)")) {
        renderDoc.capture->requestAfterFrames(120);
    }
    ImGui::EndDisabled();
}

void renderDebugPrimitivesContent(App& app)
{
    auto* runtime = app.getRenderRuntime();
    if (!runtime) {
        return;
    }

    auto& debugSystem = runtime->getDebugRenderSystem();
    auto settings = debugSystem.buildSettingsSnapshot();
    bool changed = false;
    changed |= ImGui::Checkbox("Enabled", &settings.bEnabled);
    changed |= ImGui::Checkbox("Depth Test", &settings.bDepthTest);
    changed |= ImGui::Checkbox("Draw Lines", &settings.bDrawLines);
    changed |= ImGui::Checkbox("Draw Shapes", &settings.bDrawShapes);
    ImGui::Separator();
    ImGui::Text("Pending: %zu lines, %zu shapes", settings.pendingLineCount, settings.pendingShapeCount);
    ImGui::Text("Frame: %zu lines, %zu shapes", settings.frameLineCount, settings.frameShapeCount);
    ImGui::Text("Immediate: %zu lines, %zu shapes", settings.immediateLineCount, settings.immediateShapeCount);
    if (changed) {
        debugSystem.requestSettings(settings);
    }

    if (ImGui::TreeNode("Pipelines")) {
        renderGraphicsPipelineInspector("Lines", debugSystem.getPrimitives()._linePipeline.get());
        renderGraphicsPipelineInspector("Shapes", debugSystem.getPrimitives()._shapePipeline.get());
        ImGui::TreePop();
    }
}

void renderPostProcessingTechnicalContent(const PostProcessingStage& stage)
{
    if (stage._bloomProcessor && ImGui::TreeNode("Bloom")) {
        ImGui::Text("Blur Passes (H+V): %u", stage._bloomProcessor->_lastBlurPassCount);
        renderGraphicsPipelineInspector("Extract", stage._bloomProcessor->_extractPipeline.get());
        renderGraphicsPipelineInspector("Blur", stage._bloomProcessor->_blurPipeline.get());
        renderGraphicsPipelineInspector("Composite", stage._bloomProcessor->_compositePipeline.get());
        ImGui::TreePop();
    }

    if (stage._postProcessor) {
        renderGraphicsPipelineInspector("Basic Postprocess Pipeline", stage._postProcessor->_pipeline.get());
    }
}

void renderShadowStageInternals(ShadowStage& stage)
{
    auto* basicTechnique = dynamic_cast<BasicShadowMapTechnique*>(stage.getTechnique());
    if (!basicTechnique) {
        ImGui::TextDisabled("Unsupported shadow technique.");
        return;
    }

    if (ImGui::TreeNode("Stats")) {
        const auto& settings = basicTechnique->getSettings();
        ImGui::Text("Technique: Basic Shadow Map");
        ImGui::Text("Resolution: %u", settings.resolution);
        ImGui::Text("Point lights: %u / %u", basicTechnique->getLastPreparedPointLightCount(), settings.getEffectivePointLightCount());
        ImGui::Text("Point Indirect: %s", settings.pointLightUseIndirect ? "On" : "Off");
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Passes")) {
        if (ImGui::TreeNode("Directional")) {
            ImGui::TextDisabled("No runtime controls");
            renderGraphicsPipelineInspector("Directional Static", basicTechnique->getDirectionalPass().getStaticPipeline());
            renderGraphicsPipelineInspector("Directional Skinned", basicTechnique->getDirectionalPass().getSkinnedPipeline());
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Point")) {
            ImGui::Text("Indirect Path: %s", basicTechnique->getPointPass().getIndirectRenderer().isSupported() ? "supported" : "unsupported");
            renderGraphicsPipelineInspector("Point Static", basicTechnique->getPointPass().getDirectStaticPipeline());
            renderGraphicsPipelineInspector("Point Skinned", basicTechnique->getPointPass().getDirectSkinnedPipeline());
            ImGui::TreePop();
        }
        ImGui::TreePop();
    }
}

void renderDeferredPerformanceContent(DeferredRenderPipeline& pipeline)
{
    auto& perf = profiling::metrics();
    auto metric = [&perf](FName sampleKey, FName metricKey) {
        return perf.getDisplayValue(sampleKey, metricKey);
    };
    auto cpu = [&metric](FName sampleKey) {
        return metric(sampleKey, perf::metric::cpuTimeMs());
    };

    const float frameCpuMs        = cpu(perf::sample::renderFrame());
    const float frameGpuMs        = metric(perf::sample::renderFrame(), perf::metric::gpuTimeMs());
    const float logicMs           = cpu(perf::sample::frameLogic());
    const float renderMs          = cpu(perf::sample::frameRender());
    const float automationMs      = cpu(perf::sample::frameAutomation());
    const float unaccountedMs     = cpu(perf::sample::frameUnaccounted());
    const float extractMs         = cpu(perf::sample::renderExtract());
    const float runtimeMs         = cpu(perf::sample::renderRuntime());
    const float prepareFrameMs    = cpu(perf::sample::renderPrepareFrame());
    const float waitIdleMs        = cpu(perf::sample::renderWaitIdle());
    const float beginMs           = cpu(perf::sample::renderBegin());
    const float waitFenceMs       = cpu(perf::sample::vulkanWaitFence());
    const float acquireMs         = cpu(perf::sample::vulkanAcquire());
    const float worldMs           = cpu(perf::sample::renderWorld());
    const float deferredTickMs    = cpu(perf::sample::deferredTick());
    const float shadowMs          = cpu(perf::sample::deferredShadow());
    const float gbufferMs         = cpu(perf::sample::deferredGBuffer());
    const float depthCopyMs       = cpu(perf::sample::deferredDepthCopy());
    const float lightMs           = cpu(perf::sample::deferredLight());
    const float overlayMs         = cpu(perf::sample::deferredOverlay());
    const float viewportOverlayMs = cpu(perf::sample::renderViewportOverlay());
    const float postProcessMs     = cpu(perf::sample::renderPostProcess());
    const float presentationMs    = cpu(perf::sample::renderPresentation());
    const float renderCallbacksMs = cpu(perf::sample::frameRenderCallbacks());
    const float submitMs          = cpu(perf::sample::renderSubmit());
    const float presentMs         = cpu(perf::sample::vulkanPresent());

    ImGui::Text("CPU frame: %.3f ms", frameCpuMs);
    ImGui::Text("GPU frame: %.3f ms", frameGpuMs);
    ImGui::Text("Draw items: %u", pipeline._lastDrawCount);
    ImGui::Text("Point lights: %u", pipeline._lastPointLightCount);

    renderPerfTree("Frame Cycle", frameCpuMs, [&]() {
        renderPerfLeaf("Logic", logicMs, frameCpuMs);
        renderPerfTree("Render", renderMs, [&]() {
            renderPerfLeaf("Extract", extractMs, renderMs);
            renderPerfTree("Runtime", runtimeMs, [&]() {
                renderPerfTree("PrepareFrame", prepareFrameMs, [&]() {
                    renderPerfLeaf("WaitIdle", waitIdleMs, prepareFrameMs);
                    renderPerfTree("Begin", beginMs, [&]() {
                        renderPerfLeaf("WaitFence", waitFenceMs, beginMs);
                        renderPerfLeaf("Acquire", acquireMs, beginMs);
                    });
                });
                renderPerfTree("World", worldMs, [&]() {
                    renderPerfTree("Deferred", deferredTickMs, [&]() {
                        renderPerfLeaf("Shadow", shadowMs, deferredTickMs);
                        renderPerfLeaf("GBuffer", gbufferMs, deferredTickMs);
                        renderPerfLeaf("DepthCopy", depthCopyMs, deferredTickMs);
                        renderPerfLeaf("Light", lightMs, deferredTickMs);
                        renderPerfLeaf("Overlay", overlayMs, deferredTickMs);
                    });
                    renderPerfLeaf("ViewportOverlay", viewportOverlayMs, worldMs);
                    renderPerfLeaf("PostProcess", postProcessMs, worldMs);
                });
                renderPerfLeaf("Presentation", presentationMs, runtimeMs);
                renderPerfLeaf("RenderCallbacks", renderCallbacksMs, runtimeMs);
                renderPerfTree("Submit", submitMs, [&]() {
                    renderPerfLeaf("Present", presentMs, submitMs);
                });
            });
        });
        renderPerfLeaf("Automation", automationMs, frameCpuMs);
        renderPerfLeaf("Unaccounted", unaccountedMs, frameCpuMs);
    });
}

void renderDeferredStageInternals(DeferredRenderPipeline& pipeline)
{
    if (pipeline._shadowStage && ImGui::TreeNode("Shadow")) {
        renderShadowStageInternals(*pipeline._shadowStage);
        ImGui::TreePop();
    }

    if (pipeline._gBufferStage && ImGui::TreeNode("GBuffer")) {
        if (ImGui::TreeNode("Stats")) {
            ImGui::Text("Point shadow budget: %u", pipeline._gBufferStage->getMaxShadowedPointLights());
            ImGui::Text("Shadowed point lights: %u", pipeline._gBufferStage->getLastShadowedPointLights());
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Pipelines")) {
            renderGraphicsPipelineInspector("PBR Static", pipeline._gBufferStage->getPBRPipeline());
            renderGraphicsPipelineInspector("PBR Skinned", pipeline._gBufferStage->getPBRSkinnedPipeline());
            renderGraphicsPipelineInspector("Phong Static", pipeline._gBufferStage->getPhongPipeline());
            renderGraphicsPipelineInspector("Phong Skinned", pipeline._gBufferStage->getPhongSkinnedPipeline());
            renderGraphicsPipelineInspector("Unlit Static", pipeline._gBufferStage->getUnlitPipeline());
            renderGraphicsPipelineInspector("Unlit Skinned", pipeline._gBufferStage->getUnlitSkinnedPipeline());
            ImGui::TreePop();
        }
        ImGui::TreePop();
    }

    if (pipeline._ssaoStage && ImGui::TreeNode("SSAO")) {
        ImGui::Text("Descriptor writes: %u", pipeline._ssaoStage->getLastInputDescriptorWriteCount());
        renderGraphicsPipelineInspector("SSAO Pipeline", pipeline._ssaoStage->getPipeline());
        ImGui::TreePop();
    }

    if (pipeline._lightStage && ImGui::TreeNode("Lighting")) {
        if (ImGui::TreeNode("Performance")) {
            auto& perf = profiling::metrics();
            ImGui::Text("Light prepare CPU: %.3f ms", perf.getDisplayValue(perf::sample::deferredLightPrepare(), perf::metric::cpuTimeMs()));
            ImGui::Text("Light execute CPU: %.3f ms", perf.getDisplayValue(perf::sample::deferredLightExecute(), perf::metric::cpuTimeMs()));
            ImGui::Text("Descriptor writes: gbuffer=%u shadow=%u",
                        pipeline._lightStage->getLastGBufferDescriptorWriteCount(),
                        pipeline._lightStage->getLastShadowDescriptorWriteCount());
            ImGui::TreePop();
        }
        renderGraphicsPipelineInspector("Light Pipeline", pipeline._lightStage->getPipeline());
        ImGui::TreePop();
    }

    if (pipeline._overlayStage && ImGui::TreeNode("Viewport Overlay")) {
        if (ImGui::TreeNode("Pipelines")) {
            renderGraphicsPipelineInspector("Skybox", pipeline._overlayStage->getSkyboxPipeline());
            renderGraphicsPipelineInspector("Overlay", pipeline._overlayStage->getOverlayPipeline());
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Debug")) {
            if (auto* debugRender = pipeline._overlayStage->getDebugRenderSystem()) {
                auto settings = debugRender->buildSettingsSnapshot();
                ImGui::Text("Debug primitives: %s", settings.bEnabled ? "enabled" : "disabled");
            }

            auto& debugSkinning = pipeline._overlayStage->getDebugSkinning();
            ImGui::Checkbox("Enable Debug Skinning", &debugSkinning.bEnabled);
            ImGui::DragInt("Picking Bone", &debugSkinning.pickingBone, 1.0f, 0, DebugSkinning::BONE_COUNT - 1);
            renderGraphicsPipelineInspector("Debug Skinning Pipeline", debugSkinning.getPipeline());
            ImGui::TreePop();
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Post Process")) {
        renderPostProcessingTechnicalContent(pipeline._postProcessStage);
        ImGui::TreePop();
    }
}

void renderForwardStageInternals(ForwardRenderPipeline& pipeline)
{
    if (pipeline._shadowStage && ImGui::TreeNode("Shadow")) {
        renderShadowStageInternals(*pipeline._shadowStage);
        ImGui::TreePop();
    }

    if (pipeline._viewportStage && ImGui::TreeNode("Viewport Renderer")) {
        auto& viewportStage = *pipeline._viewportStage;
        auto& auxPasses     = viewportStage.getAuxPasses();
        auto& litPasses     = viewportStage.getLitPasses();
        auto& unlitPass     = viewportStage.getUnlitPass();

        if (ImGui::TreeNode("Settings")) {
            int colorType = auxPasses.getSimpleDefaultColorType();
            if (ImGui::Combo("Simple Color Type", &colorType, "Normal\0UV\0Fixed\0")) {
                auxPasses.setSimpleDefaultColorType(colorType);
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Debug")) {
            if (ImGui::TreeNode("Phong Debug")) {
                auto& phongDebug = litPasses.phongDebug();
                bool bDebugNormal = phongDebug.bDebugNormal != 0;
                bool bDebugDepth  = phongDebug.bDebugDepth != 0;
                bool bDebugUV     = phongDebug.bDebugUV != 0;
                if (ImGui::Checkbox("Debug Normal", &bDebugNormal)) {
                    phongDebug.bDebugNormal = bDebugNormal ? 1u : 0u;
                }
                if (ImGui::Checkbox("Debug Depth", &bDebugDepth)) {
                    phongDebug.bDebugDepth = bDebugDepth ? 1u : 0u;
                }
                if (ImGui::Checkbox("Debug UV", &bDebugUV)) {
                    phongDebug.bDebugUV = bDebugUV ? 1u : 0u;
                }
                ImGui::DragFloat4("Float Param", glm::value_ptr(phongDebug.floatParam), 0.1f);
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Debug Render")) {
                int mode = static_cast<int>(auxPasses.getDebugMode());
                if (ImGui::Combo("Mode", &mode, "None\0NormalColor\0NormalDir\0Depth\0UV\0")) {
                    auxPasses.setDebugMode(static_cast<ForwardViewportAuxPasses::EDebugMode>(mode));
                }
                ImGui::DragFloat4("Float Param", glm::value_ptr(auxPasses.getDebugUBO().floatParam), 0.1f);
                ImGui::TreePop();
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Pipelines")) {
            renderGraphicsPipelineInspector("Unlit Static", unlitPass.getStaticVariant().pipeline.get());
            renderGraphicsPipelineInspector("Unlit Skinned", unlitPass.getSkinnedVariant().pipeline.get());
            renderGraphicsPipelineInspector("Phong Static", litPasses.getPhongStaticVariant().pipeline.get());
            renderGraphicsPipelineInspector("Phong Skinned", litPasses.getPhongSkinnedVariant().pipeline.get());
            renderGraphicsPipelineInspector("PBR Static", litPasses.getPBRStaticVariant().pipeline.get());
            renderGraphicsPipelineInspector("PBR Skinned", litPasses.getPBRSkinnedVariant().pipeline.get());
            renderGraphicsPipelineInspector("Simple", auxPasses.getSimplePipeline());
            renderGraphicsPipelineInspector("Skybox", auxPasses.getSkyboxPipeline());
            renderGraphicsPipelineInspector("Debug Pipeline", auxPasses.getDebugPipeline());
            ImGui::TreePop();
        }

        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Post Process")) {
        renderPostProcessingTechnicalContent(pipeline._postProcessStage);
        ImGui::TreePop();
    }
}

void renderRenderingInternalsContent(App& app)
{
    if (auto* deferred = getDeferredPipeline(app)) {
        if (ImGui::TreeNode("Runtime Perf")) {
            renderDeferredPerformanceContent(*deferred);
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Stage Internals")) {
            renderDeferredStageInternals(*deferred);
            ImGui::TreePop();
        }
        return;
    }

    if (auto* forward = getForwardPipeline(app)) {
        if (ImGui::TreeNode("Stage Internals")) {
            renderForwardStageInternals(*forward);
            ImGui::TreePop();
        }
    }
}

} // namespace

void migrateLegacyRuntimeSettings()
{
    migrateLegacyRuntimeSetting<bool>("render.deferred.reverseViewportY");
    migrateLegacyRuntimeSetting<bool>("render.deferred.ssaoEnabled");
    migrateLegacyRuntimeSetting<float>("render.deferred.ssao.radius");
    migrateLegacyRuntimeSetting<float>("render.deferred.ssao.bias");
    migrateLegacyRuntimeSetting<float>("render.deferred.ssao.power");
    migrateLegacyRuntimeSetting<float>("render.deferred.ssao.intensity");
    migrateLegacyRuntimeSetting<bool>("render.deferred.light.enablePBRDiffuseIBL");
    migrateLegacyRuntimeSetting<bool>("render.deferred.light.enablePBRSpecularIBL");
    migrateLegacyRuntimeSetting<bool>("render.postprocess.basic.inversion");
    migrateLegacyRuntimeSetting<int>("render.postprocess.basic.grayscale");
    migrateLegacyRuntimeSetting<int>("render.postprocess.basic.kernel");
    migrateLegacyRuntimeSetting<float>("render.postprocess.basic.kernelTexelOffset");
    migrateLegacyRuntimeSetting<bool>("render.postprocess.basic.tonemapping.enabled");
    migrateLegacyRuntimeSetting<int>("render.postprocess.basic.tonemapping.curve");
    migrateLegacyRuntimeSetting<float>("render.postprocess.basic.tonemapping.exposure");
    migrateLegacyRuntimeSetting<bool>("render.postprocess.basic.output.gammaCorrection");
    migrateLegacyRuntimeSetting<float>("render.postprocess.basic.output.gamma");
    migrateLegacyRuntimeSetting<bool>("render.postprocess.basic.output.randomGrain");
    migrateLegacyRuntimeSetting<float>("render.postprocess.basic.output.randomGrainStrength");
    migrateLegacyRuntimeSetting<bool>("render.postprocess.bloom.enabled");
    migrateLegacyRuntimeSetting<float>("render.postprocess.bloom.threshold");
    migrateLegacyRuntimeSetting<float>("render.postprocess.bloom.softKnee");
    migrateLegacyRuntimeSetting<float>("render.postprocess.bloom.extractIntensity");
    migrateLegacyRuntimeSetting<int>("render.postprocess.bloom.blurPasses");
    migrateLegacyRuntimeSetting<float>("render.postprocess.bloom.strength");
}

void renderRuntimeToolsWindow(App& app, EditorLayer& layer, FreeCameraController& controller, float dt)
{
    if (!ImGui::Begin("Runtime Tools")) {
        ImGui::End();
        return;
    }

    if (ImGui::CollapsingHeader("Stats", ImGuiTreeNodeFlags_DefaultOpen)) {
        renderFrameStatsContent(app, dt);
    }
    if (ImGui::CollapsingHeader("Session", ImGuiTreeNodeFlags_DefaultOpen)) {
        renderSessionContent(app);
    }
    if (ImGui::CollapsingHeader("Profiling")) {
        renderProfilingContent();
    }
    if (ImGui::CollapsingHeader("Render Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (auto* runtime = app.getRenderRuntime()) {
            renderPresentationSettings(app, *runtime);
            ImGui::Separator();
            if (runtime->isDeferredPipelineActive()) {
                renderDeferredSettingsContent(app);
            }
            else {
                renderForwardSettingsContent(app);
            }
        }
    }
    if (ImGui::CollapsingHeader("Rendering Internals", ImGuiTreeNodeFlags_DefaultOpen)) {
        renderRenderingInternalsContent(app);
    }
    if (ImGui::CollapsingHeader("Editor Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        renderEditorCameraContent(layer, controller);
    }
    if (ImGui::CollapsingHeader("Clear Values")) {
        renderClearValuesContent();
    }
    if (ImGui::CollapsingHeader("Render2D Debug")) {
        renderRender2DDebugContent();
    }
    if (ImGui::CollapsingHeader("Diagnostics")) {
        renderDiagnosticsContent(app);
    }
    if (ImGui::CollapsingHeader("Debug Primitives")) {
        renderDebugPrimitivesContent(app);
    }
    if (ImGui::CollapsingHeader("Render Targets")) {
        renderRenderTargetInspectorContent(app);
    }

    ImGui::End();
}

} // namespace ya
