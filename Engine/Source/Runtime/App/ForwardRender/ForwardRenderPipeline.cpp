#include "Runtime/App/ForwardRender/ForwardRenderPipeline.h"

#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Render/Core/Buffer.h"
#include "Render/Core/Sampler.h"
#include "Render/Core/Texture.h"
#include <glm/gtc/matrix_transform.hpp>

namespace ya
{

void ForwardRenderPipeline::rebuildShadowViews()
{
    _shadowResources.rebuildViews(_render, "Shadow Map");

    std::vector<DescriptorImageInfo> pointInfos(MAX_POINT_LIGHTS);
    for (uint32_t i = 0; i < MAX_POINT_LIGHTS; ++i) {
        pointInfos[i] = DescriptorImageInfo{
            .imageView   = _shadowResources.pointCubeIVs[i] ? _shadowResources.pointCubeIVs[i]->getHandle() : ImageViewHandle{},
            .sampler     = _shadowResources.sampler ? _shadowResources.sampler->getHandle() : SamplerHandle{},
            .imageLayout = EImageLayout::ShaderReadOnlyOptimal,
        };
    }

    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::writeOneImage(depthBufferShadowDS, 0, _shadowResources.directionalDepthIV.get(), _shadowResources.sampler.get()),
        WriteDescriptorSet{
            .dstSet          = depthBufferShadowDS,
            .dstBinding      = 1,
            .dstArrayElement = 0,
            .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
            .descriptorCount = MAX_POINT_LIGHTS,
            .imageInfos      = pointInfos,
        },
    });

    if (_shadowStage && _shadowResources.depthImage) {
        _shadowStage->refreshShadowResources(
            _shadowResources.depthImage,
            _shadowResources.depthFormat,
            _shadowResources.extent);
    }
}

void ForwardRenderPipeline::init(const InitDesc& desc)
{
    _render                 = desc.render;
    _shadowSettings         = desc.shadowSettings;
    _getFrameIndex          = desc.getFrameIndex;
    _getElapsedTimeSeconds  = desc.getElapsedTimeSeconds;
    getActiveScene          = desc.getActiveScene;
    getResourceResolveSystem = desc.getResourceResolveSystem;
    getSceneSkyboxDescriptorSet = desc.getSceneSkyboxDescriptorSet;
    getSceneEnvironmentLightingDescriptorSet = desc.getSceneEnvironmentLightingDescriptorSet;
    if (_shadowSettings) {
        _frameShadowSettings = *_shadowSettings;
    }

    initViewportResources(desc);
    initPostProcessResources(desc);
    initShadowResources();
    initStageResources();

    _render->waitIdle();
}

void ForwardRenderPipeline::initViewportResources(const InitDesc& desc)
{
    viewportRT = ya::createRenderTarget(RenderTargetCreateInfo{
        .label            = "Viewport RenderTarget",
        .renderingMode    = ERenderingMode::DynamicRendering,
        .bSwapChainTarget = false,
        .extent           = {.width = static_cast<uint32_t>(desc.windowW), .height = static_cast<uint32_t>(desc.windowH)},
        .frameBufferCount = 1,
        .attachments      = {
            .colorAttach = {
                AttachmentDescription{
                    .index         = 0,
                    .format        = VIEWPORT_COLOR_FORMAT,
                    .samples       = ESampleCount::Sample_1,
                    .loadOp        = EAttachmentLoadOp::Clear,
                    .storeOp       = EAttachmentStoreOp::Store,
                    .initialLayout = EImageLayout::ColorAttachmentOptimal,
                    .finalLayout   = EImageLayout::ShaderReadOnlyOptimal,
                    .usage         = EImageUsage::ColorAttachment | EImageUsage::Sampled | EImageUsage::TransferSrc,
                },
            },
            .depthAttach = AttachmentDescription{
                .index          = 1,
                .format         = DEPTH_FORMAT,
                .samples        = ESampleCount::Sample_1,
                .loadOp         = EAttachmentLoadOp::Clear,
                .storeOp        = EAttachmentStoreOp::Store,
                .stencilLoadOp  = EAttachmentLoadOp::Clear,
                .stencilStoreOp = EAttachmentStoreOp::Store,
                .initialLayout  = EImageLayout::DepthStencilAttachmentOptimal,
                .finalLayout    = EImageLayout::DepthStencilAttachmentOptimal,
                .usage          = EImageUsage::DepthStencilAttachment | EImageUsage::Sampled,
            },
        },
    });
    YA_CORE_ASSERT(viewportRT, "Failed to create viewport render target");
}

void ForwardRenderPipeline::initPostProcessResources(const InitDesc& desc)
{
    _postProcessStage.init(PostProcessingStage::InitDesc{
        .render      = _render,
        .colorFormat = POSTPROCESS_COLOR_FORMAT,
        .width       = static_cast<uint32_t>(desc.windowW),
        .height      = static_cast<uint32_t>(desc.windowH),
    });
    _deleter.push("PostProcessStage", [this](void*)
                  { _postProcessStage.shutdown(); });
}

void ForwardRenderPipeline::initShadowResources()
{
    _shadowResources.init(_render, ShadowMapResourceDesc{
        .renderTargetLabel = "Shadow Map RenderTarget",
        .samplerLabel      = "shadow",
        .viewLabelPrefix   = "Shadow Map",
        .extent            = {.width = 512, .height = 512},
        .depthFormat       = SHADOW_MAPPING_DEPTH_BUFFER_FORMAT,
    });
    _deleter.push("DepthRT", [this](void*)
                  { _shadowResources.renderTarget.reset(); });

    _descriptorPool = IDescriptorPool::create(_render, DescriptorPoolCreateInfo{
                                                           .label     = "ForwardPipeline Descriptor Pool",
                                                           .maxSets   = 200,
                                                           .poolSizes = {{.type = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = (1 + MAX_POINT_LIGHTS)}},
                                                       });
    _deleter.push("OwnedDescriptorPool", [this](void*)
                  { _descriptorPool.reset(); });

    depthBufferDSL      = IDescriptorSetLayout::create(_render, DescriptorSetLayoutDesc{
                                                                    .label    = "DepthBuffer_DSL",
                                                                    .bindings = {
                                                                        {.binding = 0, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                                                                        {.binding = 1, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = MAX_POINT_LIGHTS, .stageFlags = EShaderStage::Fragment},
                                                                    },
                                                                });
    depthBufferShadowDS = _descriptorPool->allocateDescriptorSets(depthBufferDSL);
    _render->as<VulkanRender>()->setDebugObjectName(VK_OBJECT_TYPE_DESCRIPTOR_SET, depthBufferShadowDS.ptr, "DepthBuffer_Shadow_DS");
    _deleter.push("DepthBufferDSL", [this](void*)
                  { depthBufferDSL.reset(); });

    _deleter.push("ShadowSampler", [this](void*)
                  { _shadowResources.sampler.reset(); });

    _render->waitIdle();
    rebuildShadowViews();

    _deleter.push("Shadow ImageViews", [this](void*)
                  { _shadowResources.destroy(); });
}

void ForwardRenderPipeline::initStageResources()
{
    _shadowStage = ya::makeShared<ShadowStage>();
    _shadowStage->setRenderTarget(_shadowResources.renderTarget);
    _shadowStage->init(_render);
    if (_shadowResources.depthImage) {
        _shadowStage->refreshShadowResources(
            _shadowResources.depthImage,
            _shadowResources.depthFormat,
            _shadowResources.extent);
    }

    PipelineRenderingInfo viewportPRI{
        .label                  = "Forward Viewport",
        .colorAttachmentFormats = {VIEWPORT_COLOR_FORMAT},
        .depthAttachmentFormat  = DEPTH_FORMAT,
    };
    _viewportStage = ya::makeShared<ForwardViewportStage>();
    _viewportStage->initWithDesc(ForwardViewportStage::InitDesc{
        .render                             = _render,
        .renderPass                         = nullptr,
        .pipelineRenderingInfo              = viewportPRI,
        .depthBufferShadowDS                = depthBufferShadowDS,
        .shadowState                        = buildShadowState(),
        .getFrameIndex                      = _getFrameIndex,
        .getElapsedTimeSeconds              = _getElapsedTimeSeconds,
        .getActiveScene                     = getActiveScene,
        .getResourceResolveSystem           = getResourceResolveSystem,
        .getSceneSkyboxDescriptorSet        = getSceneSkyboxDescriptorSet,
        .getSceneEnvironmentLightingDescriptorSet = getSceneEnvironmentLightingDescriptorSet,
    });

    _deleter.push("Stages", [this](void*)
                  {
        if (_viewportStage) { _viewportStage->destroy(); _viewportStage.reset(); }
        if (_shadowStage) { _shadowStage->destroy(); _shadowStage.reset(); } });
}

void ForwardRenderPipeline::tick(const RenderPipelineFrameContext& frame)
{
    if (shouldSkipTick(frame)) {
        return;
    }

    RenderStageContext stageCtx{};
    beginTick(frame, stageCtx);
    refreshDirtyResources();
    syncShadowSettings();
    executeShadowPass(stageCtx);
    executeViewportPass(frame, stageCtx);
    finalizeViewportPass(frame.cmdBuf);
}

bool ForwardRenderPipeline::shouldSkipTick(const RenderPipelineFrameContext& frame) const
{
    YA_CORE_ASSERT(frame.cmdBuf, "ForwardRenderPipeline requires command buffer");
    return frame.viewportRect.extent.x <= 0 || frame.viewportRect.extent.y <= 0;
}

void ForwardRenderPipeline::beginTick(const RenderPipelineFrameContext& frame, RenderStageContext& stageCtx)
{
    const uint32_t vpW = static_cast<uint32_t>(frame.viewportRect.extent.x);
    const uint32_t vpH = static_cast<uint32_t>(frame.viewportRect.extent.y);

    stageCtx = RenderStageContext{
        .cmdBuf         = frame.cmdBuf,
        .frameData      = frame.frameData,
        .flightIndex    = frame.flightIndex,
        .deltaTime      = frame.deltaTime,
        .viewportExtent = {.width = vpW, .height = vpH},
    };

    _postProcessStage.beginFrame();
    captureShadowSettings(frame);
}

void ForwardRenderPipeline::refreshDirtyResources()
{
    const bool bViewportDirty         = viewportRT && viewportRT->isDirty();
    const bool bViewportPipelineDirty = viewportRT && viewportRT->hasAttachmentDirty();
    const bool bShadowDirty           = _shadowResources.isDirty();

    if (bViewportDirty) {
        flushViewportResources();
    }
    if (bShadowDirty) {
        flushShadowResources();
    }

    if (bViewportPipelineDirty && _viewportStage) {
        refreshViewportStageState();
    }
    if (bShadowDirty) {
        refreshShadowStageState();
    }
}

void ForwardRenderPipeline::flushViewportResources()
{
    if (viewportRT) {
        viewportRT->flushIfDirty();
    }
}

void ForwardRenderPipeline::flushShadowResources()
{
    _shadowResources.flushIfDirty();
}

void ForwardRenderPipeline::refreshViewportStageState()
{
    if (_viewportStage) {
        _viewportStage->refreshPipelineFormats(viewportRT.get());
    }
}

void ForwardRenderPipeline::refreshShadowStageState()
{
    rebuildShadowViews();
}

void ForwardRenderPipeline::syncShadowSettings()
{
    if (_viewportStage) {
        _viewportStage->applyShadowState(buildShadowState());
    }
}

void ForwardRenderPipeline::captureShadowSettings(const RenderPipelineFrameContext& frame)
{
    if (frame.shadowSettings) {
        _frameShadowSettings = *frame.shadowSettings;
    }
    else if (_shadowSettings) {
        _frameShadowSettings = *_shadowSettings;
    }
}

ShadowSettings ForwardRenderPipeline::currentShadowSettings() const
{
    return _frameShadowSettings;
}

bool ForwardRenderPipeline::isShadowMappingEnabled() const
{
    return currentShadowSettings().isEnabled();
}

void ForwardRenderPipeline::applyShadowSettings(const ShadowSettings& shadowSettings)
{
    _frameShadowSettings = shadowSettings;
    if (_shadowSettings) {
        *_shadowSettings = shadowSettings;
    }
    syncShadowSettings();
}

ShadowRuntimeState ForwardRenderPipeline::buildShadowState() const
{
    ShadowRuntimeState shadowState{};
    const ShadowSettings shadowSettings = currentShadowSettings();
    shadowState.bEnableShadowMapping    = shadowSettings.isEnabled();
    shadowState.bEnablePointLightShadow = shadowSettings.pointLightEnabled;
    shadowState.maxShadowedPointLights  = shadowSettings.getEffectivePointLightCount();
    shadowState.filter                  = shadowSettings.filter;
    shadowState.bias                    = shadowSettings.bias;
    shadowState.normalBias              = shadowSettings.normalBias;
    shadowState.shadowMapResolution     = _shadowResources.extent.width > 0
        ? _shadowResources.extent.width
        : std::max(shadowSettings.resolution, 1u);

    if (shadowState.bEnableShadowMapping && _shadowResources.directionalDepthIV && _shadowResources.sampler) {
        shadowState.directionalDepthIV = _shadowResources.directionalDepthIV.get();
        shadowState.sampler            = _shadowResources.sampler.get();
        for (uint32_t lightIndex = 0; lightIndex < MAX_POINT_LIGHTS; ++lightIndex) {
            shadowState.pointCubeDepthIVs[lightIndex] = _shadowResources.pointCubeIVs[lightIndex].get();
        }
    }

    return shadowState;
}

void ForwardRenderPipeline::executeShadowPass(RenderStageContext& stageCtx)
{
    const ShadowSettings shadowSettings = currentShadowSettings();
    if (!shadowSettings.isEnabled() || !_shadowStage) {
        return;
    }

    _shadowStage->applySettings(shadowSettings);
    _shadowStage->prepare(stageCtx);
    _shadowStage->execute(stageCtx);
}

void ForwardRenderPipeline::executeViewportPass(const RenderPipelineFrameContext& frame, RenderStageContext& stageCtx)
{
    _viewportStage->prepare(stageCtx);

    auto extent = Extent2D::fromVec2(frame.viewportRect.extent / frame.viewportFrameBufferScale);
    viewportRT->setExtent(extent);

    RenderingInfo ri{
        .label            = "ViewPort",
        .renderArea       = Rect2D{.pos = {0, 0}, .extent = viewportRT->getExtent().toVec2()},
        .layerCount       = 1,
        .colorClearValues = {ClearValue(0.0f, 0.0f, 0.0f, 1.0f)},
        .depthClearValue  = ClearValue(1.0f, 0),
        .renderTarget     = viewportRT.get(),
    };

    frame.cmdBuf->beginRendering(ri);

    stageCtx.viewportExtent = viewportRT->getExtent();
    _viewportStage->execute(stageCtx);

    _viewportRI         = ri;
    _lastTickCtx        = frame.frameData ? frame.frameData->toFrameContext() : FrameContext{
                                                                                  .view       = frame.view,
                                                                                  .projection = frame.projection,
                                                                                  .cameraPos  = frame.cameraPos,
                                                                              };
    _lastTickCtx.extent = viewportRT->getExtent();
    _lastFrameInput     = frame;
}

void ForwardRenderPipeline::finalizeViewportPass(ICommandBuffer* cmdBuf)
{
    if (_lastFrameInput.recordViewportOverlays) {
        YA_PERF_SCOPE(perf::sample::renderViewportOverlay(), perf::metric::cpuTimeMs(), perf::domain::render());
        _lastFrameInput.recordViewportOverlays(cmdBuf, viewportRT->getExtent(), _lastTickCtx);
    }

    cmdBuf->endRendering(_viewportRI);

    auto* inputTexture = bMSAA ? viewportRT->getCurrentResolveTexture() : viewportRT->getCurrentColorTexture(0);

    viewportTexture = _postProcessStage.execute(
        cmdBuf, inputTexture, _lastFrameInput.viewportRect.extent, &_lastTickCtx);

    YA_CORE_ASSERT(viewportTexture, "Failed to get viewport texture for postprocessing");
}

void ForwardRenderPipeline::shutdown()
{
    _getFrameIndex = {};
    _getElapsedTimeSeconds = {};
    getActiveScene = {};
    getResourceResolveSystem = {};
    getSceneSkyboxDescriptorSet = {};
    getSceneEnvironmentLightingDescriptorSet = {};
    _deleter.clear();
}

void ForwardRenderPipeline::renderSettingsGUI()
{
    renderGeneralSettingsGUI();

    if (ImGui::TreeNode("Shadows")) {
        renderShadowSettingsGUI();
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Post Process")) {
        renderPostProcessSettingsGUI();
        ImGui::TreePop();
    }
}

void ForwardRenderPipeline::renderGeneralSettingsGUI()
{
}

void ForwardRenderPipeline::renderShadowSettingsGUI()
{
    if (!_shadowSettings) {
        return;
    }
    const ShadowSettings currentShadowSettings = *_shadowSettings;
    ShadowSettings       pendingShadowSettings = currentShadowSettings;

    bool bEnabled = pendingShadowSettings.isEnabled();
    if (ImGui::Checkbox("Shadow Mapping", &bEnabled)) {
        if (bEnabled) {
            if (pendingShadowSettings.quality == EShadowQuality::Off) {
                pendingShadowSettings.applyQualityPreset(EShadowQuality::Medium);
            }
        }
        else {
            pendingShadowSettings.quality = EShadowQuality::Off;
        }
        applyShadowSettings(pendingShadowSettings);
    }

    if (pendingShadowSettings.isEnabled()) {
        bool bDirty = false;
        bDirty |= ImGui::Checkbox("Point Light Shadow", &pendingShadowSettings.pointLightEnabled);
        int maxPL = static_cast<int>(pendingShadowSettings.maxPointLightShadows);
        if (ImGui::SliderInt("Max Point Shadows", &maxPL, 0, MAX_POINT_LIGHTS)) {
            pendingShadowSettings.maxPointLightShadows = static_cast<uint32_t>(maxPL);
            bDirty = true;
        }
        bDirty |= ImGui::Checkbox("Point Light Indirect Draw", &pendingShadowSettings.pointLightUseIndirect);
        bDirty |= ImGui::Checkbox("Point Light Indirect Cull", &pendingShadowSettings.pointLightIndirectCullEnabled);
        if (bDirty) {
            applyShadowSettings(pendingShadowSettings);
        }
    }
}

void ForwardRenderPipeline::renderPostProcessSettingsGUI()
{
    _postProcessStage.renderSettingsGUI();
}

void ForwardRenderPipeline::renderTechnicalGUI()
{
    renderPerformanceGUI();
    renderStageInternalsGUI();
}

void ForwardRenderPipeline::renderPerformanceGUI()
{
}

void ForwardRenderPipeline::renderStageInternalsGUI()
{
    if (_shadowStage) {
        _shadowStage->renderGUI();
    }
    if (_viewportStage) {
        _viewportStage->renderGUI();
    }
    if (ImGui::TreeNode("Post Process")) {
        _postProcessStage.renderTechnicalGUI();
        ImGui::TreePop();
    }
}

void ForwardRenderPipeline::renderGUI(bool bRenderTreeNode)
{
    if (bRenderTreeNode && !ImGui::TreeNode("Forward Render Pipeline")) return;

    renderSettingsGUI();

    if (ImGui::TreeNode("Pipeline Internals")) {
        renderTechnicalGUI();
        ImGui::TreePop();
    }

    if (bRenderTreeNode) { ImGui::TreePop(); }
}

void ForwardRenderPipeline::onViewportResized(Rect2D rect)
{
    Extent2D newExtent{
        .width  = static_cast<uint32_t>(rect.extent.x),
        .height = static_cast<uint32_t>(rect.extent.y),
    };
    if (viewportRT) viewportRT->setExtent(newExtent);
    _postProcessStage.onViewportResized(newExtent);
}

Extent2D ForwardRenderPipeline::getViewportExtent() const
{
    return viewportRT ? viewportRT->getExtent() : Extent2D{};
}

IImageView* ForwardRenderPipeline::getShadowPointFaceDepthIV(uint32_t pointLightIndex, uint32_t faceIndex) const
{
    if (pointLightIndex >= MAX_POINT_LIGHTS || faceIndex >= 6) return nullptr;
    return _shadowResources.pointFaceIVs[pointLightIndex][faceIndex].get();
}

} // namespace ya
