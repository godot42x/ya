#include "DeferredRenderPipeline.h"

#include "Config/ConfigManager.h"
#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Core/Profiling/Profiling.h"
#include "Render/Core/Sampler.h"
#include "Render/Core/Texture.h"
#include "Runtime/App/App.h"
#include <algorithm>
#include <chrono>
#include <format>

namespace ya
{

namespace
{

constexpr const char* DEFERRED_PIPELINE_CONFIG_DOC_NAME                       = "editor";
constexpr const char* DEFERRED_PIPELINE_CONFIG_KEY_ENABLE_SHADOW_MAPPING      = "render.deferred.shadow.enableShadowMapping";
constexpr const char* DEFERRED_PIPELINE_CONFIG_KEY_ENABLE_POINT_LIGHT_SHADOW  = "render.deferred.shadow.enablePointLightShadow";
constexpr const char* DEFERRED_PIPELINE_CONFIG_KEY_MAX_POINT_LIGHT_SHADOWS    = "render.deferred.shadow.maxPointLightShadowCount";
constexpr const char* DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_QUALITY             = "render.deferred.shadow.quality";
constexpr const char* DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_DIRECTIONAL_ENABLED = "render.deferred.shadow.directionalEnabled";
constexpr const char* DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_POINT_ENABLED       = "render.deferred.shadow.pointLightEnabled";
constexpr const char* DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_POINT_INDIRECT      = "render.deferred.shadow.pointLightUseIndirect";
constexpr const char* DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_POINT_CULL          = "render.deferred.shadow.pointLightIndirectCullEnabled";
constexpr const char* DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_RESOLUTION          = "render.deferred.shadow.resolution";
constexpr const char* DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_FILTER              = "render.deferred.shadow.filter";
constexpr const char* DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_BIAS                = "render.deferred.shadow.bias";
constexpr const char* DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_NORMAL_BIAS         = "render.deferred.shadow.normalBias";
constexpr const char* DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_DIRECTIONAL_DIST    = "render.deferred.shadow.directionalDistance";
constexpr const char* DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_DIRECTIONAL_CASCADE = "render.deferred.shadow.directionalCascades";
constexpr const char* DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_DIRECTIONAL_STABLE  = "render.deferred.shadow.directionalStableFit";
constexpr const char* DEFERRED_PIPELINE_CONFIG_KEY_ENABLE_SSAO                = "render.deferred.ssao.enabled";

void drawPerfLeaf(const char* label, float value, float parentValue = 0.0f)
{
    constexpr ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_Bullet;
    if (parentValue > 0.0f) {
        ImGui::TreeNodeEx(label, flags, "%s  %.3f ms  %.1f%%", label, value, value * 100.0f / parentValue);
        return;
    }
    ImGui::TreeNodeEx(label, flags, "%s  %.3f ms", label, value);
}

template <typename Fn>
void drawPerfNode(const char* label, float value, Fn&& drawChildren)
{
    if (ImGui::TreeNode(label, "%s  %.3f ms", label, value)) {
        drawChildren();
        ImGui::TreePop();
    }
}

} // namespace

DeferredRenderPipeline::~DeferredRenderPipeline()
{
    shutdown();
}

void DeferredRenderPipeline::initRenderTargets(Extent2D extent)
{
    _gBufferRT = createRenderTarget(RenderTargetCreateInfo{
        .label            = "GBuffer RenderTarget",
        .renderingMode    = ERenderingMode::DynamicRendering,
        .bSwapChainTarget = false,
        .extent           = extent,
        .frameBufferCount = 1,
        .attachments      = {

            .colorAttach = {
                AttachmentDescription{
                    .index         = 0,
                    .format        = SIGNED_LINEAR_FORMAT,
                    .initialLayout = EImageLayout::ColorAttachmentOptimal,
                    .finalLayout   = EImageLayout::ShaderReadOnlyOptimal,
                    .usage         = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                },
                AttachmentDescription{
                    .index         = 1,
                    .format        = SIGNED_LINEAR_FORMAT,
                    .initialLayout = EImageLayout::ColorAttachmentOptimal,
                    .finalLayout   = EImageLayout::ShaderReadOnlyOptimal,
                    .usage         = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                },
                AttachmentDescription{
                    .index         = 2,
                    .format        = LINEAR_FORMAT,
                    .initialLayout = EImageLayout::ColorAttachmentOptimal,
                    .finalLayout   = EImageLayout::ShaderReadOnlyOptimal,
                    .usage         = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                },
                AttachmentDescription{
                    .index         = 3,
                    .format        = SHADING_MODEL_FORMAT,
                    .initialLayout = EImageLayout::ColorAttachmentOptimal,
                    .finalLayout   = EImageLayout::ShaderReadOnlyOptimal,
                    .usage         = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                },
            },
            .depthAttach = AttachmentDescription{
                .index          = 4,
                .format         = DEPTH_FORMAT,
                .loadOp         = EAttachmentLoadOp::Clear,
                .storeOp        = EAttachmentStoreOp::Store,
                .stencilLoadOp  = EAttachmentLoadOp::Clear,
                .stencilStoreOp = EAttachmentStoreOp::Store,
                .initialLayout  = EImageLayout::DepthStencilAttachmentOptimal,
                .finalLayout    = EImageLayout::ShaderReadOnlyOptimal,
                .usage          = EImageUsage::DepthStencilAttachment | EImageUsage::Sampled | EImageUsage::TransferSrc,
            },
        },
    });

    _viewportRT = createRenderTarget(RenderTargetCreateInfo{
        .label            = "Deferred Viewport RT",
        .bSwapChainTarget = false,
        .extent           = extent,
        .attachments      = {
                 .colorAttach = {
                AttachmentDescription{
                         .index          = 0,
                         .format         = VIEWPORT_COLOR_FORMAT,
                         .samples        = ESampleCount::Sample_1,
                         .loadOp         = EAttachmentLoadOp::Clear,
                         .storeOp        = EAttachmentStoreOp::Store,
                         .stencilLoadOp  = EAttachmentLoadOp::DontCare,
                         .stencilStoreOp = EAttachmentStoreOp::DontCare,
                         .initialLayout  = EImageLayout::ColorAttachmentOptimal,
                         .finalLayout    = EImageLayout::ShaderReadOnlyOptimal,
                         .usage          = EImageUsage::ColorAttachment | EImageUsage::Sampled | EImageUsage::TransferSrc,
                },
            },
                 .depthAttach = AttachmentDescription{
                     .index          = 1,
                     .format         = DEPTH_FORMAT,
                     .samples        = ESampleCount::Sample_1,
                     .loadOp         = EAttachmentLoadOp::Load,
                     .storeOp        = EAttachmentStoreOp::Store,
                     .stencilLoadOp  = EAttachmentLoadOp::DontCare,
                     .stencilStoreOp = EAttachmentStoreOp::DontCare,
                     .initialLayout  = EImageLayout::DepthStencilAttachmentOptimal,
                     .finalLayout    = EImageLayout::ShaderReadOnlyOptimal,
                     .usage          = EImageUsage::DepthStencilAttachment | EImageUsage::Sampled | EImageUsage::TransferDst,
            },
        },
    });
}

void DeferredRenderPipeline::initShadowResources()
{
    if (!_render || _shadowResources.renderTarget) {
        return;
    }

    const auto& shadowSettings      = App::get()->getShadowSettings();
    const uint32_t shadowResolution = std::max(shadowSettings.resolution, 1u);

    _shadowResources.init(_render, ShadowMapResourceDesc{
        .renderTargetLabel = "Deferred Shadow Map RenderTarget",
        .samplerLabel      = "deferred-shadow",
        .viewLabelPrefix   = "Deferred Shadow",
        .extent            = {.width = shadowResolution, .height = shadowResolution},
        .depthFormat       = _shadowDepthFormat,
    });
}

void DeferredRenderPipeline::destroyShadowResources()
{
    if (_shadowStage) {
        _shadowStage->destroy();
        _shadowStage.reset();
    }

    _shadowResources.destroy();
}

void DeferredRenderPipeline::syncShadowSettings()
{
    const auto shadowState = buildShadowState();

    if (_lightStage) {
        _lightStage->applyShadowState(shadowState);
    }

    if (_gBufferStage) {
        _gBufferStage->applyShadowState(shadowState);
    }
}

ShadowRuntimeState DeferredRenderPipeline::buildShadowState() const
{
    ShadowRuntimeState shadowState{};
    shadowState.settings = App::get()->getShadowSettings();
    shadowState.bEnableShadowMapping    = shadowState.settings.isEnabled();
    shadowState.bEnablePointLightShadow = shadowState.settings.pointLightEnabled;
    shadowState.maxShadowedPointLights  = shadowState.settings.getEffectivePointLightCount();
    shadowState.shadowMapResolution = _shadowResources.renderTarget ? _shadowResources.renderTarget->getExtent().width : std::max(shadowState.settings.resolution, 1u);

    if (shadowState.bEnableShadowMapping && _shadowResources.directionalDepthIV && _shadowResources.sampler) {
        shadowState.directionalDepthIV = _shadowResources.directionalDepthIV.get();
        shadowState.sampler            = _shadowResources.sampler.get();
        for (uint32_t lightIndex = 0; lightIndex < MAX_POINT_LIGHTS; ++lightIndex) {
            shadowState.pointCubeDepthIVs[lightIndex] = _shadowResources.pointCubeIVs[lightIndex].get();
        }
    }

    return shadowState;
}

void DeferredRenderPipeline::queueShadowSettingsChange(const ShadowSettings& shadowSettings)
{
    _pendingShadowSettings = shadowSettings;
    saveShadowSettingsToConfig(_pendingShadowSettings);

    if (_bShadowSettingsChangePending) {
        return;
    }

    _bShadowSettingsChangePending = true;
    App::get()->taskManager.registerFrameTask(
        [this]()
        {
            _bShadowSettingsChangePending = false;

            applyShadowSettings(_pendingShadowSettings);
        });
}

void DeferredRenderPipeline::applyShadowSettings(const ShadowSettings& shadowSettings)
{
    auto& appShadowSettings = App::get()->getShadowSettings();
    const bool bWasEnabled   = appShadowSettings.isEnabled();
    const bool bWillEnable   = shadowSettings.isEnabled();
    const bool bToggleChanged = bWasEnabled != bWillEnable;

    if (_render && bToggleChanged) {
        _render->waitIdle();
    }

    appShadowSettings = shadowSettings;

    if (appShadowSettings.isEnabled()) {
        if (!_shadowResources.renderTarget) {
            initShadowResources();
        }
        if (!_shadowStage && _shadowResources.renderTarget) {
            _shadowStage = ya::makeShared<ShadowStage>();
            _shadowStage->setRenderTarget(_shadowResources.renderTarget);
            _shadowStage->init(_render);
        }
    }
    else {
        destroyShadowResources();
    }

    syncShadowSettings();
    saveShadowSettingsToConfig(appShadowSettings);
}

void DeferredRenderPipeline::loadPersistentSettings()
{
    auto& cfgManager = ConfigManager::get();

    const bool bEnableShadowMapping = cfgManager.getOr<bool>(DEFERRED_PIPELINE_CONFIG_DOC_NAME,
                                                             DEFERRED_PIPELINE_CONFIG_KEY_ENABLE_SHADOW_MAPPING,
                                                             App::get()->getShadowSettings().isEnabled());
    _bEnableSSAO                    = cfgManager.getOr<bool>(DEFERRED_PIPELINE_CONFIG_DOC_NAME,
                                          DEFERRED_PIPELINE_CONFIG_KEY_ENABLE_SSAO,
                                          _bEnableSSAO);
    const bool bEnablePointLightShadow = cfgManager.getOr<bool>(DEFERRED_PIPELINE_CONFIG_DOC_NAME,
                                                                DEFERRED_PIPELINE_CONFIG_KEY_ENABLE_POINT_LIGHT_SHADOW,
                                                                App::get()->getShadowSettings().pointLightEnabled);

    // Sync loaded config to App-layer ShadowSettings
    auto& shadowSettings = App::get()->getShadowSettings();
    int qualityValue = cfgManager.getOr<int>(DEFERRED_PIPELINE_CONFIG_DOC_NAME,
                                             DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_QUALITY,
                                             static_cast<int>(shadowSettings.quality));
    qualityValue = std::clamp(qualityValue,
                              static_cast<int>(EShadowQuality::Off),
                              static_cast<int>(EShadowQuality::Ultra));
    shadowSettings.quality = static_cast<EShadowQuality::T>(qualityValue);
    shadowSettings.directionalEnabled = cfgManager.getOr<bool>(DEFERRED_PIPELINE_CONFIG_DOC_NAME,
                                                               DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_DIRECTIONAL_ENABLED,
                                                               shadowSettings.directionalEnabled);
    shadowSettings.pointLightEnabled = cfgManager.getOr<bool>(DEFERRED_PIPELINE_CONFIG_DOC_NAME,
                                                              DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_POINT_ENABLED,
                                                              bEnablePointLightShadow);
    shadowSettings.pointLightUseIndirect = cfgManager.getOr<bool>(DEFERRED_PIPELINE_CONFIG_DOC_NAME,
                                                                  DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_POINT_INDIRECT,
                                                                  shadowSettings.pointLightUseIndirect);
    shadowSettings.pointLightIndirectCullEnabled = cfgManager.getOr<bool>(DEFERRED_PIPELINE_CONFIG_DOC_NAME,
                                                                          DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_POINT_CULL,
                                                                          shadowSettings.pointLightIndirectCullEnabled);
    shadowSettings.maxPointLightShadows = static_cast<uint32_t>(std::clamp(
        cfgManager.getOr<int>(DEFERRED_PIPELINE_CONFIG_DOC_NAME,
                              DEFERRED_PIPELINE_CONFIG_KEY_MAX_POINT_LIGHT_SHADOWS,
                              static_cast<int>(shadowSettings.maxPointLightShadows)),
        0,
        static_cast<int>(MAX_POINT_LIGHTS)));
    shadowSettings.resolution = static_cast<uint32_t>(std::clamp(
        cfgManager.getOr<int>(DEFERRED_PIPELINE_CONFIG_DOC_NAME,
                              DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_RESOLUTION,
                              static_cast<int>(shadowSettings.resolution)),
        128,
        8192));
    shadowSettings.filter = static_cast<EShadowFilter::T>(std::clamp(
        cfgManager.getOr<int>(DEFERRED_PIPELINE_CONFIG_DOC_NAME,
                              DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_FILTER,
                              static_cast<int>(shadowSettings.filter)),
        static_cast<int>(EShadowFilter::Hard),
        static_cast<int>(EShadowFilter::PCF_High)));
    shadowSettings.bias = cfgManager.getOr<float>(DEFERRED_PIPELINE_CONFIG_DOC_NAME,
                                                  DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_BIAS,
                                                  shadowSettings.bias);
    shadowSettings.normalBias = cfgManager.getOr<float>(DEFERRED_PIPELINE_CONFIG_DOC_NAME,
                                                        DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_NORMAL_BIAS,
                                                        shadowSettings.normalBias);
    shadowSettings.directionalDistance = cfgManager.getOr<float>(DEFERRED_PIPELINE_CONFIG_DOC_NAME,
                                                                 DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_DIRECTIONAL_DIST,
                                                                 shadowSettings.directionalDistance);
    shadowSettings.directionalStableFit = cfgManager.getOr<bool>(DEFERRED_PIPELINE_CONFIG_DOC_NAME,
                                                                 DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_DIRECTIONAL_STABLE,
                                                                 shadowSettings.directionalStableFit);
    shadowSettings.directionalCascades = static_cast<uint32_t>(std::clamp(
        cfgManager.getOr<int>(DEFERRED_PIPELINE_CONFIG_DOC_NAME,
                              DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_DIRECTIONAL_CASCADE,
                              static_cast<int>(shadowSettings.directionalCascades)),
        0,
        4));

    if (!bEnableShadowMapping || shadowSettings.quality == EShadowQuality::Off) {
        shadowSettings.quality = EShadowQuality::Off;
    }
    shadowSettings.pointLightEnabled    = bEnablePointLightShadow && shadowSettings.pointLightEnabled;
    shadowSettings.maxPointLightShadows = std::min(shadowSettings.maxPointLightShadows, static_cast<uint32_t>(MAX_POINT_LIGHTS));

    const auto& automationShadowOverrides = App::get()->getDesc().automation.shadow;
    if (automationShadowOverrides.quality) {
        shadowSettings.applyQualityPreset(*automationShadowOverrides.quality);
    }
    if (automationShadowOverrides.directionalEnabled) {
        shadowSettings.directionalEnabled = *automationShadowOverrides.directionalEnabled;
    }
    if (automationShadowOverrides.pointLightEnabled) {
        shadowSettings.pointLightEnabled = *automationShadowOverrides.pointLightEnabled;
    }
    if (automationShadowOverrides.pointLightUseIndirect) {
        shadowSettings.pointLightUseIndirect = *automationShadowOverrides.pointLightUseIndirect;
    }
    if (automationShadowOverrides.pointLightIndirectCullEnabled) {
        shadowSettings.pointLightIndirectCullEnabled = *automationShadowOverrides.pointLightIndirectCullEnabled;
    }
    if (automationShadowOverrides.maxPointLightShadows) {
        shadowSettings.maxPointLightShadows = *automationShadowOverrides.maxPointLightShadows;
    }
    if (automationShadowOverrides.filter) {
        shadowSettings.filter = *automationShadowOverrides.filter;
    }
    if (automationShadowOverrides.bias) {
        shadowSettings.bias = *automationShadowOverrides.bias;
    }
    if (automationShadowOverrides.normalBias) {
        shadowSettings.normalBias = *automationShadowOverrides.normalBias;
    }
    if (automationShadowOverrides.directionalDistance) {
        shadowSettings.directionalDistance = *automationShadowOverrides.directionalDistance;
    }
    _pendingShadowSettings = shadowSettings;
}

void DeferredRenderPipeline::saveShadowSettingsToConfig(const ShadowSettings& shadowSettings) const
{
    ConfigManager::Editor(DEFERRED_PIPELINE_CONFIG_DOC_NAME)
        .set(DEFERRED_PIPELINE_CONFIG_KEY_ENABLE_SHADOW_MAPPING, shadowSettings.isEnabled())
        .set(DEFERRED_PIPELINE_CONFIG_KEY_ENABLE_POINT_LIGHT_SHADOW, shadowSettings.pointLightEnabled)
        .set(DEFERRED_PIPELINE_CONFIG_KEY_MAX_POINT_LIGHT_SHADOWS,
             static_cast<int>(std::min(shadowSettings.maxPointLightShadows, static_cast<uint32_t>(MAX_POINT_LIGHTS))))
        .set(DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_QUALITY, static_cast<int>(shadowSettings.quality))
        .set(DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_DIRECTIONAL_ENABLED, shadowSettings.directionalEnabled)
        .set(DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_POINT_ENABLED, shadowSettings.pointLightEnabled)
        .set(DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_POINT_INDIRECT, shadowSettings.pointLightUseIndirect)
        .set(DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_POINT_CULL, shadowSettings.pointLightIndirectCullEnabled)
        .set(DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_RESOLUTION, static_cast<int>(shadowSettings.resolution))
        .set(DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_FILTER, static_cast<int>(shadowSettings.filter))
        .set(DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_BIAS, shadowSettings.bias)
        .set(DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_NORMAL_BIAS, shadowSettings.normalBias)
        .set(DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_DIRECTIONAL_DIST, shadowSettings.directionalDistance)
        .set(DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_DIRECTIONAL_STABLE, shadowSettings.directionalStableFit)
        .set(DEFERRED_PIPELINE_CONFIG_KEY_SHADOW_DIRECTIONAL_CASCADE, static_cast<int>(shadowSettings.directionalCascades));
}

void DeferredRenderPipeline::rebuildShadowViews()
{
    _shadowResources.rebuildViews(_render, "Deferred Shadow");
}

// ═══════════════════════════════════════════════════════════════════════
// Init / Shutdown
// ═══════════════════════════════════════════════════════════════════════

void DeferredRenderPipeline::init(const InitDesc& desc)
{
    shutdown();

    initPipelineState(desc);
    initStages();

    _render->waitIdle();
}

void DeferredRenderPipeline::initPipelineState(const InitDesc& desc)
{
    _render                       = desc.render;
    _bViewportPassOpen            = false;
    _bShadowSettingsChangePending = false;
    loadPersistentSettings();
    YA_CORE_ASSERT(_render, "DeferredRenderPipeline requires a valid render backend");

    Extent2D extent{
        .width  = static_cast<uint32_t>(desc.windowW),
        .height = static_cast<uint32_t>(desc.windowH),
    };

    initRenderTargets(extent);
    _ssaoTexture = Texture::createRenderTexture(RenderTextureCreateInfo{
        .label   = "DeferredSSAO",
        .width   = extent.width,
        .height  = extent.height,
        .format  = SSAOStage::AO_FORMAT,
        .usage   = EImageUsage::ColorAttachment | EImageUsage::Sampled,
        .samples = ESampleCount::Sample_1,
        .isDepth = false,
    });
    if (App::get()->getShadowSettings().isEnabled()) {
        initShadowResources();
    }

    _postProcessStage.init(PostProcessingStage::InitDesc{
        .render      = _render,
        .colorFormat = POSTPROCESS_COLOR_FORMAT,
        .width       = extent.width,
        .height      = extent.height,
    });
}

void DeferredRenderPipeline::initStages()
{
    if (_shadowResources.renderTarget) {
        _shadowStage = ya::makeShared<ShadowStage>();
        _shadowStage->setRenderTarget(_shadowResources.renderTarget);
        _shadowStage->init(_render);
    }

    _gBufferStage = ya::makeShared<GBufferStage>();
    _gBufferStage->init(_render);

    _ssaoStage = ya::makeShared<SSAOStage>();
    _ssaoStage->setup(_gBufferRT.get(), _ssaoTexture.get());
    _ssaoStage->setSettings(_ssaoStage->getRadius(), _ssaoStage->getBias(), _ssaoStage->getPower(), _ssaoStage->getIntensity(), _bReverseViewportY);
    _ssaoStage->init(_render);

    _lightStage = ya::makeShared<LightStage>();
    _lightStage->setup(_gBufferStage.get(), _gBufferRT.get());
    _lightStage->setSSAOTexture(_ssaoTexture.get());
    _lightStage->init(_render);
    syncShadowSettings();

    _overlayStage = ya::makeShared<ViewportOverlayStage>();
    _overlayStage->init(_render);
}

void DeferredRenderPipeline::shutdown()
{
    _bViewportPassOpen = false;
    _postProcessStage.shutdown();
    viewportTexture = nullptr;

    _debugAlbedoRGBView.reset();
    _debugSpecularAlphaView.reset();
    _cachedAlbedoSpecImageViewHandle = nullptr;

    if (_overlayStage) {
        _overlayStage->destroy();
        _overlayStage.reset();
    }
    if (_lightStage) {
        _lightStage->destroy();
        _lightStage.reset();
    }
    if (_ssaoStage) {
        _ssaoStage->destroy();
        _ssaoStage.reset();
    }
    if (_gBufferStage) {
        _gBufferStage->destroy();
        _gBufferStage.reset();
    }

    _ssaoTexture.reset();
    _viewportRT.reset();
    _gBufferRT.reset();
    destroyShadowResources();
    _bShadowSettingsChangePending = false;
    _render                       = nullptr;
}


// ═══════════════════════════════════════════════════════════════════════
// Tick
// ═══════════════════════════════════════════════════════════════════════

void DeferredRenderPipeline::tick(const RenderPipelineFrameContext& frame)
{
    frame.cmdBuf->debugBeginLabel("Deferred Pipeline");

    if (shouldSkipTick(frame)) {
        return;
    }

    YA_PERF_SCOPE(perf::sample::deferredTick(), perf::metric::cpuTimeMs(), perf::domain::render());

    RenderStageContext stageCtx{};
    uint32_t           vpW = 0;
    uint32_t           vpH = 0;
    beginTick(frame, stageCtx, vpW, vpH);
    refreshDirtyResources();
    syncFrameSettings(frame);
    executeShadowPass(stageCtx);
    executeGBufferPass(frame, stageCtx, vpW, vpH);
    executeSSAOPass(stageCtx);
    executeDepthCopyPass(frame.cmdBuf);

    beginViewportRendering(frame);
    executeViewportPass(frame, stageCtx);

    frame.cmdBuf->debugEndLabel();
}

bool DeferredRenderPipeline::shouldSkipTick(const RenderPipelineFrameContext& frame) const
{
    YA_CORE_ASSERT(frame.cmdBuf, "DeferredRenderPipeline requires a command buffer");

    if (frame.viewportRect.extent.x <= 0 || frame.viewportRect.extent.y <= 0) {
        frame.cmdBuf->debugEndLabel();
        return true;
    }

    if (!frame.frameData) {
        frame.cmdBuf->debugEndLabel();
        return true;
    }

    return false;
}

void DeferredRenderPipeline::beginTick(const RenderPipelineFrameContext& frame, RenderStageContext& stageCtx, uint32_t& vpW, uint32_t& vpH)
{
    _postProcessStage.beginFrame();

    vpW = static_cast<uint32_t>(frame.viewportRect.extent.x);
    vpH = static_cast<uint32_t>(frame.viewportRect.extent.y);

    _lastPointLightCount = frame.frameData->numPointLights;
    _lastDrawCount       = static_cast<uint32_t>(frame.frameData->totalDrawCount());

    stageCtx = RenderStageContext{
        .cmdBuf         = frame.cmdBuf,
        .frameData      = frame.frameData,
        .flightIndex    = frame.flightIndex,
        .deltaTime      = frame.deltaTime,
        .viewportExtent = {.width = vpW, .height = vpH},
    };
}

void DeferredRenderPipeline::refreshDirtyResources()
{
    const bool bViewportPipelineDirty = _viewportRT && _viewportRT->hasDirtyReason(ERenderTargetDirtyReason::Attachments);
    const bool bGBufferDirty          = _gBufferRT && _gBufferRT->bDirty;
    const bool bGBufferPipelineDirty  = _gBufferRT && _gBufferRT->hasDirtyReason(ERenderTargetDirtyReason::Attachments);
    _viewportRT->flushDirty();
    _gBufferRT->flushDirty();
    const bool bShadowResourcesDirty = _shadowResources.renderTarget && _shadowResources.renderTarget->bDirty;
    const bool bShadowPipelineDirty  = _shadowResources.renderTarget && _shadowResources.renderTarget->hasDirtyReason(ERenderTargetDirtyReason::Attachments);
    if (_shadowResources.renderTarget) {
        _shadowResources.renderTarget->flushDirty();
    }

    if (bGBufferDirty) {
        _cachedAlbedoSpecImageViewHandle = nullptr;
        _debugAlbedoRGBView.reset();
        _debugSpecularAlphaView.reset();
        if (_ssaoStage) {
            _ssaoStage->invalidateInputDescriptors();
        }
        if (_lightStage) {
            _lightStage->invalidateGBufferDescriptors();
        }
        if (bGBufferPipelineDirty && _gBufferStage) {
            _gBufferStage->refreshPipelineFormats(_gBufferRT.get());
        }
    }

    if (_ssaoTexture && _gBufferRT && _ssaoTexture->getExtent() != _gBufferRT->getExtent()) {
        _render->waitIdle();
        _ssaoTexture = Texture::createRenderTexture(RenderTextureCreateInfo{
            .label   = "DeferredSSAO",
            .width   = _gBufferRT->getExtent().width,
            .height  = _gBufferRT->getExtent().height,
            .format  = SSAOStage::AO_FORMAT,
            .usage   = EImageUsage::ColorAttachment | EImageUsage::Sampled,
            .samples = ESampleCount::Sample_1,
            .isDepth = false,
        });
        if (_ssaoStage) {
            _ssaoStage->setup(_gBufferRT.get(), _ssaoTexture.get());
            _ssaoStage->refreshPipelineFormat();
        }
        if (_lightStage) {
            _lightStage->setSSAOTexture(_ssaoTexture.get());
        }
    }

    if (bViewportPipelineDirty) {
        if (_lightStage) {
            _lightStage->refreshPipelineFormats(_viewportRT.get());
        }
        if (_overlayStage) {
            _overlayStage->refreshPipelineFormats(_viewportRT.get());
        }
    }

    if (bShadowResourcesDirty) {
        rebuildShadowViews();
        if (bShadowPipelineDirty && _shadowStage) {
            _shadowStage->refreshPipelineFromRenderTarget();
        }
        syncShadowSettings();
    }
}

void DeferredRenderPipeline::executeSSAOPass(const RenderStageContext& stageCtx)
{
    if (!_bEnableSSAO || !_ssaoStage) {
        return;
    }

    _ssaoStage->prepare(stageCtx);
    _ssaoStage->execute(stageCtx);
}

void DeferredRenderPipeline::syncFrameSettings(const RenderPipelineFrameContext& frame)
{
    (void)frame;

    if (_lightStage) {
        _lightStage->setSSAOTexture(_bEnableSSAO ? _ssaoTexture.get() : nullptr);
    }

    if (_ssaoStage) {
        _ssaoStage->setSettings(_ssaoStage->getRadius(),
                                _ssaoStage->getBias(),
                                _ssaoStage->getPower(),
                                _ssaoStage->getIntensity(),
                                _bReverseViewportY);
    }

    const auto&    shadowSettings           = App::get()->getShadowSettings();
    const uint32_t shadowedPointLightBudget = shadowSettings.getEffectivePointLightCount();
    const uint32_t desiredShadowResolution  = std::max(shadowSettings.resolution, 1u);
    if (shadowSettings.isEnabled()) {
        const bool bShadowResolutionDirty = !_shadowResources.renderTarget ||
                                            _shadowResources.renderTarget->getExtent().width != desiredShadowResolution ||
                                            _shadowResources.renderTarget->getExtent().height != desiredShadowResolution;
        if (bShadowResolutionDirty) {
            if (_render) {
                _render->waitIdle();
            }
            destroyShadowResources();
            initShadowResources();
            if (!_shadowStage && _shadowResources.renderTarget) {
                _shadowStage = ya::makeShared<ShadowStage>();
                _shadowStage->setRenderTarget(_shadowResources.renderTarget);
                _shadowStage->init(_render);
            }
            syncShadowSettings();
        }
    }

    (void)shadowedPointLightBudget;
    (void)shadowSettings;
    (void)desiredShadowResolution;
    syncShadowSettings();
}

void DeferredRenderPipeline::executeShadowPass(RenderStageContext& stageCtx)
{
    const auto& shadowSettings = App::get()->getShadowSettings();
    if (_shadowStage && shadowSettings.isEnabled()) {
        _shadowStage->applySettings(shadowSettings);
        {
            YA_PERF_SCOPE(perf::sample::deferredShadow(), perf::metric::cpuTimeMs(), perf::domain::render());
            _shadowStage->prepare(stageCtx);
            _shadowStage->execute(stageCtx);
        }
        handoffShadowDepthForSampling(stageCtx.cmdBuf);
        return;
    }

    PerfState::Get().clearMetric(perf::sample::deferredShadow(), perf::metric::cpuTimeMs());
}

void DeferredRenderPipeline::handoffShadowDepthForSampling(ICommandBuffer* cmdBuf)
{
    auto* shadowFrameBuffer  = _shadowResources.renderTarget ? _shadowResources.renderTarget->getCurFrameBuffer() : nullptr;
    auto* shadowDepthTexture = shadowFrameBuffer ? shadowFrameBuffer->getDepthTexture() : nullptr;
    auto* shadowDepthImage   = shadowDepthTexture ? shadowDepthTexture->getImage() : nullptr;
    if (!cmdBuf || !_shadowResources.renderTarget || !shadowDepthImage) {
        return;
    }

    ImageSubresourceRange shadowDepthRange{
        .aspectMask     = EImageAspect::Depth,
        .baseMipLevel   = 0,
        .levelCount     = 1,
        .baseArrayLayer = 0,
        .layerCount     = _shadowResources.renderTarget->_layerCount,
    };
    cmdBuf->transitionImageLayoutAuto(shadowDepthImage, EImageLayout::ShaderReadOnlyOptimal, &shadowDepthRange);
}

void DeferredRenderPipeline::executeGBufferPass(const RenderPipelineFrameContext& frame, const RenderStageContext& stageCtx, uint32_t vpW, uint32_t vpH)
{
    YA_PERF_SCOPE(perf::sample::deferredGBuffer(), perf::metric::cpuTimeMs(), perf::domain::render());
    _gBufferStage->prepare(stageCtx);

    RenderingInfo gBufferRI{
        .label            = "GBuffer Pass",
        .renderArea       = Rect2D{.pos = {0, 0}, .extent = _gBufferRT->getExtent().toVec2()},
        .layerCount       = 1,
        .colorClearValues = {
            ClearValue(0.0f, 0.0f, 0.0f, 1.0f),
            ClearValue(0.0f, 0.0f, 0.0f, 1.0f),
            ClearValue(0.0f, 0.0f, 0.0f, 0.0f),
            ClearValue(0.0f, 0.0f, 0.0f, 0.0f),
        },
        .depthClearValue = ClearValue(1.0f, 0),
        .renderTarget    = _gBufferRT.get(),
    };
    frame.cmdBuf->beginRendering(gBufferRI);

    float gbVpY = 0.0f;
    float gbVpH = static_cast<float>(vpH);
    if (_bReverseViewportY) {
        gbVpY = static_cast<float>(vpH);
        gbVpH = -gbVpH;
    }
    frame.cmdBuf->setViewport(0.0f, gbVpY, static_cast<float>(vpW), gbVpH);
    frame.cmdBuf->setScissor(0, 0, vpW, vpH);

    _gBufferStage->execute(stageCtx);

    frame.cmdBuf->endRendering(gBufferRI);
}

void DeferredRenderPipeline::executeDepthCopyPass(ICommandBuffer* cmdBuf)
{
    YA_PERF_SCOPE(perf::sample::deferredDepthCopy(), perf::metric::cpuTimeMs(), perf::domain::render());
    copyGBufferDepthToViewport(cmdBuf);
}

void DeferredRenderPipeline::executeViewportPass(const RenderPipelineFrameContext& frame, RenderStageContext& stageCtx)
{
    (void)frame;

    {
        YA_PERF_SCOPE(perf::sample::deferredLight(), perf::metric::cpuTimeMs(), perf::domain::render());
        _lightStage->prepare(stageCtx);
        _lightStage->execute(stageCtx);
    }

    {
        YA_PERF_SCOPE(perf::sample::deferredOverlay(), perf::metric::cpuTimeMs(), perf::domain::render());
        _overlayStage->prepare(stageCtx);
        _overlayStage->execute(stageCtx);
    }
}

// ═══════════════════════════════════════════════════════════════════════
// Depth Copy
// ═══════════════════════════════════════════════════════════════════════

void DeferredRenderPipeline::copyGBufferDepthToViewport(ICommandBuffer* cmdBuf)
{
    auto* gbufferDepth  = _gBufferRT ? _gBufferRT->getCurFrameBuffer()->getDepthTexture() : nullptr;
    auto* viewportDepth = _viewportRT ? _viewportRT->getCurFrameBuffer()->getDepthTexture() : nullptr;
    if (!cmdBuf || !gbufferDepth || !viewportDepth) {
        return;
    }

    auto* srcImage = gbufferDepth->getImage();
    auto* dstImage = viewportDepth->getImage();
    if (!srcImage || !dstImage) {
        return;
    }

    ImageSubresourceRange depthRange{
        .aspectMask     = EImageAspect::Depth,
        .baseMipLevel   = 0,
        .levelCount     = 1,
        .baseArrayLayer = 0,
        .layerCount     = 1,
    };

    cmdBuf->debugBeginLabel("Copy GBuffer Depth → Viewport");

    cmdBuf->transitionImageLayoutAuto(srcImage, EImageLayout::TransferSrc, &depthRange);
    cmdBuf->transitionImageLayoutAuto(dstImage, EImageLayout::TransferDst, &depthRange);

    cmdBuf->copyImage(
        srcImage,
        EImageLayout::TransferSrc,
        dstImage,
        EImageLayout::TransferDst,
        {
            ImageCopy{
                .srcSubresource = ImageSubresourceLayers{
                    .aspectMask     = EImageAspect::Depth,
                    .mipLevel       = 0,
                    .baseArrayLayer = 0,
                    .layerCount     = 1,
                },
                .dstSubresource = ImageSubresourceLayers{
                    .aspectMask     = EImageAspect::Depth,
                    .mipLevel       = 0,
                    .baseArrayLayer = 0,
                    .layerCount     = 1,
                },
                .extentWidth  = _gBufferRT->getExtent().width,
                .extentHeight = _gBufferRT->getExtent().height,
                .extentDepth  = 1,
            },
        });

    cmdBuf->transitionImageLayoutAuto(srcImage, EImageLayout::ShaderReadOnlyOptimal, &depthRange);
    cmdBuf->transitionImageLayoutAuto(dstImage, EImageLayout::ShaderReadOnlyOptimal, &depthRange);
    cmdBuf->debugEndLabel();
}

// ═══════════════════════════════════════════════════════════════════════
// Viewport Pass
// ═══════════════════════════════════════════════════════════════════════

void DeferredRenderPipeline::beginViewportRendering(const RenderPipelineFrameContext& frame)
{
    auto cmdBuf = frame.cmdBuf;

    _viewportDepthSpec = RenderingInfo::ImageSpec{
        .texture       = _viewportRT->getCurFrameBuffer()->getDepthTexture(),
        .loadOp        = EAttachmentLoadOp::Load,
        .storeOp       = EAttachmentStoreOp::Store,
        .initialLayout = EImageLayout::DepthStencilAttachmentOptimal,
        .finalLayout   = EImageLayout::ShaderReadOnlyOptimal,
    };

    _viewportRI = RenderingInfo{
        .label            = "Viewport Pass",
        .renderArea       = {.pos = {0, 0}, .extent = _viewportRT->getExtent().toVec2()},
        .colorClearValues = {ClearValue(0.0f, 0.0f, 0.0f, 0.0f)},
        .renderTarget     = _viewportRT.get(),
    };

    cmdBuf->beginRendering(_viewportRI);
    _bViewportPassOpen = true;

    const uint32_t vpW = static_cast<uint32_t>(frame.viewportRect.extent.x);
    const uint32_t vpH = static_cast<uint32_t>(frame.viewportRect.extent.y);

    _lastTickCtx = {
        .view       = frame.view,
        .projection = frame.projection,
        .cameraPos  = frame.cameraPos,
        .extent     = {.width = vpW, .height = vpH},
    };
    _lastFrameInput = frame;
}

void DeferredRenderPipeline::endViewportPass(ICommandBuffer* cmdBuf)
{
    if (!_bViewportPassOpen) {
        return;
    }

    cmdBuf->endRendering(_viewportRI);
    _bViewportPassOpen = false;

    auto* inputTexture = _viewportRT->getCurFrameBuffer()->getColorTexture(0);
    {
        YA_PERF_SCOPE(perf::sample::renderPostProcess(), perf::metric::cpuTimeMs(), perf::domain::render());
        viewportTexture = _postProcessStage.execute(
            cmdBuf, inputTexture, _lastFrameInput.viewportRect.extent, &_lastTickCtx);
    }
}

void DeferredRenderPipeline::onViewportResized(Rect2D rect)
{
    Extent2D newExtent{
        .width  = static_cast<uint32_t>(rect.extent.x),
        .height = static_cast<uint32_t>(rect.extent.y),
    };

    if (_gBufferRT) _gBufferRT->setExtent(newExtent);
    if (_viewportRT) _viewportRT->setExtent(newExtent);
    if (_ssaoTexture && (_ssaoTexture->getExtent().width != newExtent.width || _ssaoTexture->getExtent().height != newExtent.height)) {
        _render->waitIdle();
        _ssaoTexture = Texture::createRenderTexture(RenderTextureCreateInfo{
            .label   = "DeferredSSAO",
            .width   = newExtent.width,
            .height  = newExtent.height,
            .format  = SSAOStage::AO_FORMAT,
            .usage   = EImageUsage::ColorAttachment | EImageUsage::Sampled,
            .samples = ESampleCount::Sample_1,
            .isDepth = false,
        });
        if (_ssaoStage) {
            _ssaoStage->setup(_gBufferRT.get(), _ssaoTexture.get());
            _ssaoStage->refreshPipelineFormat();
        }
        if (_lightStage) {
            _lightStage->setSSAOTexture(_ssaoTexture.get());
        }
    }

    _cachedAlbedoSpecImageViewHandle = nullptr;
    _debugAlbedoRGBView.reset();
    _debugSpecularAlphaView.reset();
    _postProcessStage.onViewportResized(newExtent);
}

// ═══════════════════════════════════════════════════════════════════════
// GUI
// ═══════════════════════════════════════════════════════════════════════

void DeferredRenderPipeline::renderSettingsGUI()
{
    renderGeneralSettingsGUI();

    if (ImGui::TreeNode("Lighting")) {
        renderLightingSettingsGUI();
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Ambient Occlusion")) {
        renderAOSettingsGUI();
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Post Process")) {
        renderPostProcessSettingsGUI();
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Shadows")) {
        renderShadowSettingsGUI();
        ImGui::TreePop();
    }
}

void DeferredRenderPipeline::renderGeneralSettingsGUI()
{
    ImGui::Checkbox("GBuffer Reverse Viewport Y", &_bReverseViewportY);
    ImGui::TextUnformatted("GBuffer ID + switch/case Light Pass");
}

void DeferredRenderPipeline::renderLightingSettingsGUI()
{
    if (_lightStage) {
        _lightStage->renderSettingsGUI();
    }
}

void DeferredRenderPipeline::renderAOSettingsGUI()
{
    if (ImGui::Checkbox("Enable SSAO", &_bEnableSSAO)) {
        ConfigManager::Editor(DEFERRED_PIPELINE_CONFIG_DOC_NAME)
            .set(DEFERRED_PIPELINE_CONFIG_KEY_ENABLE_SSAO, _bEnableSSAO);
    }

    if (_bEnableSSAO && _ssaoStage) {
        _ssaoStage->renderSettingsGUI();
    }
}

void DeferredRenderPipeline::renderPostProcessSettingsGUI()
{
    _postProcessStage.renderSettingsGUI();
}

void DeferredRenderPipeline::renderShadowSettingsGUI()
{
    auto& shadowSettings    = App::get()->getShadowSettings();
    bool  bShadowSettingsDirty = false;

    bool bShadowEnabled = shadowSettings.isEnabled();
    if (ImGui::Checkbox("Enable Shadow Mapping", &bShadowEnabled)) {
        ShadowSettings pendingShadowSettings = shadowSettings;
        if (bShadowEnabled) {
            if (pendingShadowSettings.quality == EShadowQuality::Off) {
                pendingShadowSettings.applyQualityPreset(EShadowQuality::Medium);
            }
        }
        else {
            pendingShadowSettings.quality = EShadowQuality::Off;
        }
        queueShadowSettingsChange(pendingShadowSettings);
    }

    if (shadowSettings.isEnabled() && _shadowStage) {
        static const char* qualityNames[] = {"Low", "Medium", "High", "Ultra"};
        int                qualityIdx     = std::max(0, static_cast<int>(shadowSettings.quality) - 1);
        if (ImGui::Combo("Quality Preset", &qualityIdx, qualityNames, IM_ARRAYSIZE(qualityNames))) {
            auto newQuality = static_cast<EShadowQuality::T>(qualityIdx + 1);
            shadowSettings.applyQualityPreset(newQuality);
            bShadowSettingsDirty = true;
        }

        if (ImGui::Checkbox("Directional Shadow", &shadowSettings.directionalEnabled)) {
            bShadowSettingsDirty = true;
        }
        if (ImGui::Checkbox("Point Light Shadow", &shadowSettings.pointLightEnabled)) {
            bShadowSettingsDirty = true;
        }
        if (ImGui::Checkbox("Point Light Indirect Draw", &shadowSettings.pointLightUseIndirect)) {
            bShadowSettingsDirty = true;
        }
        if (ImGui::Checkbox("Point Light Indirect Cull", &shadowSettings.pointLightIndirectCullEnabled)) {
            bShadowSettingsDirty = true;
        }
        int maxPL = static_cast<int>(shadowSettings.maxPointLightShadows);
        if (ImGui::SliderInt("Max Point Shadows", &maxPL, 0, MAX_POINT_LIGHTS)) {
            shadowSettings.maxPointLightShadows = static_cast<uint32_t>(maxPL);
            bShadowSettingsDirty = true;
        }

        int shadowResolution = static_cast<int>(shadowSettings.resolution);
        if (ImGui::DragInt("Shadow Resolution", &shadowResolution, 16.0f, 128, 8192, "%d")) {
            shadowSettings.resolution = static_cast<uint32_t>(std::clamp(shadowResolution, 128, 8192));
            bShadowSettingsDirty = true;
        }

        if (ImGui::DragFloat("Depth Bias", &shadowSettings.bias, 0.0001f, 0.0f, 0.1f, "%.5f")) {
            bShadowSettingsDirty = true;
        }
        if (ImGui::DragFloat("Normal Bias", &shadowSettings.normalBias, 0.0001f, 0.0f, 0.1f, "%.5f")) {
            bShadowSettingsDirty = true;
        }
        if (ImGui::DragFloat("Directional Distance", &shadowSettings.directionalDistance, 0.5f, 1.0f, 500.0f, "%.1f")) {
            bShadowSettingsDirty = true;
        }
        if (ImGui::Checkbox("Stable Directional Fit", &shadowSettings.directionalStableFit)) {
            bShadowSettingsDirty = true;
        }
        int directionalCascades = static_cast<int>(shadowSettings.directionalCascades);
        if (ImGui::SliderInt("Directional Cascades", &directionalCascades, 0, 4)) {
            shadowSettings.directionalCascades = static_cast<uint32_t>(directionalCascades);
            bShadowSettingsDirty = true;
        }

        static const char* filterNames[] = {"Hard", "PCF Low", "PCF High"};
        int                currentFilter = static_cast<int>(shadowSettings.filter);
        if (ImGui::Combo("Shadow Filter", &currentFilter, filterNames, IM_ARRAYSIZE(filterNames))) {
            shadowSettings.filter = static_cast<EShadowFilter::T>(currentFilter);
            bShadowSettingsDirty = true;
        }

        if (bShadowSettingsDirty) {
            _pendingShadowSettings = shadowSettings;
            saveShadowSettingsToConfig(shadowSettings);
        }
    }
}

void DeferredRenderPipeline::renderTechnicalGUI()
{
    renderPerformanceGUI();
    renderStageInternalsGUI();
}

void DeferredRenderPipeline::renderPerformanceGUI()
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
    const float flushCallbacksMs  = cpu(perf::sample::renderFlushCallbacks());
    const float submitMs          = cpu(perf::sample::renderSubmit());
    const float presentMs         = cpu(perf::sample::vulkanPresent());

    ImGui::Text("CPU frame: %.3f ms", frameCpuMs);
    ImGui::Text("GPU frame: %.3f ms", frameGpuMs);

    if (ImGui::TreeNode("Frame Cycle", "Frame Cycle  %.3f ms", frameCpuMs)) {
        drawPerfLeaf("Logic", logicMs, frameCpuMs);
        drawPerfNode("Render", renderMs, [&]() {
            drawPerfLeaf("Extract", extractMs, renderMs);
            drawPerfNode("Runtime", runtimeMs, [&]() {
                drawPerfNode("PrepareFrame", prepareFrameMs, [&]() {
                    drawPerfLeaf("WaitIdle", waitIdleMs, prepareFrameMs);
                    drawPerfNode("Begin", beginMs, [&]() {
                        drawPerfLeaf("WaitFence", waitFenceMs, beginMs);
                        drawPerfLeaf("Acquire", acquireMs, beginMs);
                    });
                });
                drawPerfNode("World", worldMs, [&]() {
                    drawPerfNode("Deferred", deferredTickMs, [&]() {
                        drawPerfLeaf("Shadow", shadowMs, deferredTickMs);
                        drawPerfLeaf("GBuffer", gbufferMs, deferredTickMs);
                        drawPerfLeaf("DepthCopy", depthCopyMs, deferredTickMs);
                        drawPerfLeaf("Light", lightMs, deferredTickMs);
                        drawPerfLeaf("Overlay", overlayMs, deferredTickMs);
                    });
                    drawPerfLeaf("ViewportOverlay", viewportOverlayMs, worldMs);
                    drawPerfLeaf("PostProcess", postProcessMs, worldMs);
                });
                drawPerfLeaf("Presentation", presentationMs, runtimeMs);
                drawPerfLeaf("FlushCallbacks", flushCallbacksMs, runtimeMs);
                drawPerfNode("Submit", submitMs, [&]() {
                    drawPerfLeaf("Present", presentMs, submitMs);
                });
            });
        });
        drawPerfLeaf("Automation", automationMs, frameCpuMs);
        drawPerfLeaf("Unaccounted", unaccountedMs, frameCpuMs);
        ImGui::TreePop();
    }

    ImGui::Text("Draw items: %u", _lastDrawCount);
    ImGui::Text("Point lights: %u", _lastPointLightCount);
}

void DeferredRenderPipeline::renderStageInternalsGUI()
{
    if (_shadowStage) _shadowStage->renderGUI();
    if (_gBufferStage) _gBufferStage->renderGUI();
    if (_ssaoStage && ImGui::TreeNode("SSAO")) {
        _ssaoStage->renderTechnicalGUI();
        ImGui::TreePop();
    }
    if (_lightStage && ImGui::TreeNode("Lighting")) {
        _lightStage->renderTechnicalGUI();
        ImGui::TreePop();
    }
    if (_overlayStage) _overlayStage->renderGUI();
    if (ImGui::TreeNode("Post Process")) {
        _postProcessStage.renderTechnicalGUI();
        ImGui::TreePop();
    }
}

void DeferredRenderPipeline::renderGUI(bool bRenderTreeNode)
{
    if (bRenderTreeNode && !ImGui::TreeNode("Deferred Pipeline")) return;

    renderSettingsGUI();

    if (ImGui::TreeNode("Pipeline Internals")) {
        renderTechnicalGUI();
        ImGui::TreePop();
    }

    if (bRenderTreeNode) { ImGui::TreePop(); }
}

} // namespace ya
