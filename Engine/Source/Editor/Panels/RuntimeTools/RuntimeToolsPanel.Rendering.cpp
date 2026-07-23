#include "Editor/Panels/RuntimeToolsPanelInternal.h"

namespace ya
{

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

    int shadowResolution = static_cast<int>(shadowSettings.resolution);
    if (ImGui::DragInt("Shadow Resolution", &shadowResolution, 16.0f, 128, 8192, "%d")) {
        shadowSettings.resolution = static_cast<uint32_t>(std::clamp(shadowResolution, 128, 8192));
        bDirty = true;
    }

    bDirty |= ImGui::DragFloat("Depth Bias", &shadowSettings.bias, 0.0001f, 0.0f, 0.1f, "%.5f");
    bDirty |= ImGui::DragFloat("Normal Bias", &shadowSettings.normalBias, 0.0001f, 0.0f, 0.1f, "%.5f");

    static const char* filterNames[] = {"Hard", "PCF Low", "PCF High"};
    int filter = static_cast<int>(shadowSettings.filter);
    if (ImGui::Combo("Shadow Filter", &filter, filterNames, IM_ARRAYSIZE(filterNames))) {
        shadowSettings.filter = static_cast<EShadowFilter::T>(filter);
        bDirty = true;
    }

    if (ImGui::BeginTable("ShadowLightSettings", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableSetupColumn("Directional", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Point", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::PushID("DirectionalShadowSettings");
        if (bAllowDirectionalControls) {
            bDirty |= ImGui::Checkbox("Enabled", &shadowSettings.directionalEnabled);
            ImGui::BeginDisabled(!shadowSettings.directionalEnabled);

            const bool bUseCSM = shadowSettings.directionalCascades > 1;
            if (ImGui::RadioButton("Single Map", !bUseCSM)) {
                shadowSettings.directionalCascades = 1;
                bDirty = true;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("CSM", bUseCSM)) {
                shadowSettings.directionalCascades = MAX_DIRECTIONAL_CASCADES;
                shadowSettings.resetDirectionalCascadeSplitRatios();
                bDirty = true;
            }

            bDirty |= ImGui::DragFloat("Distance", &shadowSettings.directionalDistance, 0.5f, 1.0f, 500.0f, "%.1f");
            bDirty |= ImGui::Checkbox("Stable Fit", &shadowSettings.directionalStableFit);

            if (shadowSettings.directionalCascades > 1) {
                int directionalCascades = static_cast<int>(shadowSettings.directionalCascades);
                if (ImGui::SliderInt("Cascades", &directionalCascades, 2, MAX_DIRECTIONAL_CASCADES)) {
                    shadowSettings.directionalCascades = static_cast<uint32_t>(directionalCascades);
                    shadowSettings.resetDirectionalCascadeSplitRatios();
                    bDirty = true;
                }
                for (uint32_t splitIndex = 0; splitIndex + 1 < shadowSettings.directionalCascades; ++splitIndex) {
                    const float lowerBound = splitIndex == 0
                        ? 0.001f
                        : shadowSettings.directionalCascadeSplitRatios[splitIndex - 1] + 0.001f;
                    const float upperBound = splitIndex + 2 < shadowSettings.directionalCascades
                        ? shadowSettings.directionalCascadeSplitRatios[splitIndex + 1] - 0.001f
                        : 0.999f;
                    float& splitRatio = shadowSettings.directionalCascadeSplitRatios[splitIndex];
                    splitRatio        = std::clamp(splitRatio, lowerBound, upperBound);
                    const std::string label = "Split " + std::to_string(splitIndex + 1);
                    bDirty |= ImGui::SliderFloat(label.c_str(), &splitRatio, lowerBound, upperBound, "%.3f");
                }
                bDirty |= ImGui::DragFloat("Z Range", &shadowSettings.directionalDepthRangeMultiplier, 0.1f, 1.0f, 100.0f, "%.1f");
            }
            ImGui::EndDisabled();
        }
        else {
            ImGui::TextDisabled("Unavailable in this pipeline");
        }
        ImGui::PopID();

        ImGui::TableSetColumnIndex(1);
        ImGui::PushID("PointShadowSettings");
        bDirty |= ImGui::Checkbox("Enabled", &shadowSettings.pointLightEnabled);
        ImGui::BeginDisabled(!shadowSettings.pointLightEnabled);
        bDirty |= ImGui::Checkbox("Indirect Draw", &shadowSettings.pointLightUseIndirect);
        bDirty |= ImGui::Checkbox("Indirect Cull", &shadowSettings.pointLightIndirectCullEnabled);
        int maxPointLights = static_cast<int>(shadowSettings.maxPointLightShadows);
        if (ImGui::SliderInt("Max Shadows", &maxPointLights, 0, MAX_POINT_LIGHTS)) {
            shadowSettings.maxPointLightShadows = static_cast<uint32_t>(maxPointLights);
            bDirty = true;
        }
        ImGui::EndDisabled();
        ImGui::PopID();

        ImGui::EndTable();
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

void renderDeferredSettingsContent(App& app)
{
    auto* runtime  = app.getRenderRuntime();
    auto* pipeline = getDeferredPipeline(app);
    if (!runtime || !pipeline) {
        ImGui::TextDisabled("Deferred-only settings are unavailable while the forward pipeline is active.");
        return;
    }

    auto settings = pipeline->buildSettingsSnapshot();
    bool changed  = false;

    if (ImGui::TreeNode("General")) {
        changed |= ImGui::Checkbox("GBuffer Reverse Viewport Y", &settings.bReverseViewportY);
        ImGui::TextUnformatted("GBuffer ID + switch/case Light Pass");
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Lighting")) {
        if (pipeline->_lightStage) {
            bool diffuseIBL    = pipeline->_lightStage->isPBRDiffuseIBLEnabled();
            bool specularIBL   = pipeline->_lightStage->isPBRSpecularIBLEnabled();
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
            float radius      = pipeline->_ssaoStage->getRadius();
            float bias        = pipeline->_ssaoStage->getBias();
            float power       = pipeline->_ssaoStage->getPower();
            float intensity   = pipeline->_ssaoStage->getIntensity();
            bool reverseY     = pipeline->_ssaoStage->isReverseYEnabled();
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

void renderRenderingInternalsContent(App& app)
{
    auto renderPostProcessingTechnicalContent = [](const PostProcessingStage& stage) {
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
    };

    auto renderShadowStageInternals = [](ShadowStage& stage) {
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
    };

    auto renderDeferredPerformanceContent = [](DeferredRenderPipeline& pipeline) {
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
    };

    auto renderDeferredStageInternals = [&](DeferredRenderPipeline& pipeline) {
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
    };

    auto renderForwardStageInternals = [&](ForwardRenderPipeline& pipeline) {
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
                    auto& phongDebug  = litPasses.phongDebug();
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
    };

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

} // namespace ya
