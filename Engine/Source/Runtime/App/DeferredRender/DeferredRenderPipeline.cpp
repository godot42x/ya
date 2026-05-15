#include "DeferredRenderPipeline.h"

#include "Config/ConfigManager.h"
#include "Core/Debug/PerfKeys.h"
#include "Core/Debug/PerfState.h"
#include "Render/Core/Sampler.h"
#include "Render/Core/Texture.h"
#include "Runtime/App/App.h"


#include <algorithm>
#include <chrono>

namespace ya
{

namespace
{

constexpr const char* DEFERRED_PIPELINE_CONFIG_DOC_NAME                      = "editor";
constexpr const char* DEFERRED_PIPELINE_CONFIG_KEY_ENABLE_SHADOW_MAPPING     = "render.deferred.shadow.enableShadowMapping";
constexpr const char* DEFERRED_PIPELINE_CONFIG_KEY_ENABLE_POINT_LIGHT_SHADOW = "render.deferred.shadow.enablePointLightShadow";
constexpr const char* DEFERRED_PIPELINE_CONFIG_KEY_MAX_POINT_LIGHT_SHADOWS   = "render.deferred.shadow.maxPointLightShadowCount";

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
    if (!_render || _shadowDepthRT) {
        return;
    }

    _shadowDepthRT = createRenderTarget(RenderTargetCreateInfo{
        .label            = "Deferred Shadow Map RenderTarget",
        .renderingMode    = ERenderingMode::DynamicRendering,
        .bSwapChainTarget = false,
        .extent           = {.width = 512, .height = 512},
        .frameBufferCount = 1,
        .layerCount       = 1 + MAX_POINT_LIGHTS * 6,
        .attachments      = {

            .depthAttach = AttachmentDescription{
                .index            = 0,
                .format           = _shadowDepthFormat,
                .samples          = ESampleCount::Sample_1,
                .loadOp           = EAttachmentLoadOp::Clear,
                .storeOp          = EAttachmentStoreOp::Store,
                .initialLayout    = EImageLayout::DepthStencilAttachmentOptimal,
                .finalLayout      = EImageLayout::ShaderReadOnlyOptimal,
                .usage            = EImageUsage::DepthStencilAttachment | EImageUsage::Sampled,
                .imageCreateFlags = EImageCreateFlag::CubeCompatible,
            },
        },
    });
    YA_CORE_ASSERT(_shadowDepthRT, "Failed to create deferred shadow render target");

    rebuildShadowViews();

    _shadowSampler = Sampler::create(SamplerDesc{
        .label        = "deferred-shadow",
        .minFilter    = EFilter::Linear,
        .magFilter    = EFilter::Linear,
        .mipmapMode   = ESamplerMipmapMode::Linear,
        .addressModeU = ESamplerAddressMode::ClampToBorder,
        .addressModeV = ESamplerAddressMode::ClampToBorder,
        .addressModeW = ESamplerAddressMode::ClampToBorder,
        .borderColor  = SamplerDesc::BorderColor{.type = SamplerDesc::EBorderColor::FloatOpaqueWhite, .color = {1, 1, 1, 1}},
    });
}

void DeferredRenderPipeline::destroyShadowResources()
{
    if (_shadowStage) {
        _shadowStage->destroy();
        _shadowStage.reset();
    }

    _shadowDepthRT.reset();
    _shadowDirectionalDepthIV.reset();
    for (auto& imageView : _shadowPointCubeIVs) {
        imageView.reset();
    }
    for (auto& faceViews : _shadowPointFaceIVs) {
        for (auto& imageView : faceViews) {
            imageView.reset();
        }
    }
    _shadowSampler.reset();
}

void DeferredRenderPipeline::syncShadowSettings()
{
    // Use pipeline's own flags (loaded from config) as the source of truth for
    // lightStage/gBufferStage. App::getShadowSettings() is the game-layer API
    // but these local flags are what actually control resource creation/destruction.
    const bool bShadowEnabled      = _bEnableShadowMapping;
    const bool bPointShadowEnabled = _bEnablePointLightShadow;

    if (_lightStage) {
        _lightStage->setShadowSettings(bShadowEnabled, bPointShadowEnabled);

        std::array<IImageView*, MAX_POINT_LIGHTS> shadowPointCubeViews{};
        if (bShadowEnabled && _shadowDirectionalDepthIV && _shadowSampler) {
            for (uint32_t lightIndex = 0; lightIndex < MAX_POINT_LIGHTS; ++lightIndex) {
                shadowPointCubeViews[lightIndex] = _shadowPointCubeIVs[lightIndex].get();
            }
            _lightStage->setShadowResources(_shadowDirectionalDepthIV.get(),
                                            shadowPointCubeViews,
                                            _shadowSampler.get());
        }
        else {
            _lightStage->setShadowResources(nullptr, shadowPointCubeViews, nullptr);
        }
    }

    if (_gBufferStage) {
        _gBufferStage->setMaxShadowedPointLights(
            (bShadowEnabled && bPointShadowEnabled) ? _maxPointLightShadowCount : 0);
    }
}

void DeferredRenderPipeline::queueShadowSettingsChange(bool     bEnableShadowMapping,
                                                       bool     bEnablePointLightShadow,
                                                       uint32_t maxPointLightShadowCount)
{
    maxPointLightShadowCount         = std::min(maxPointLightShadowCount, static_cast<uint32_t>(MAX_POINT_LIGHTS));
    _pendingEnableShadowMapping      = bEnableShadowMapping;
    _pendingEnablePointLightShadow   = bEnablePointLightShadow;
    _pendingMaxPointLightShadowCount = maxPointLightShadowCount;
    saveShadowSettingsToConfig(
        _pendingEnableShadowMapping,
        _pendingEnablePointLightShadow,
        _pendingMaxPointLightShadowCount);

    if (_bShadowSettingsChangePending) {
        return;
    }

    _bShadowSettingsChangePending = true;
    App::get()->taskManager.registerFrameTask(
        [this]()
        {
            _bShadowSettingsChangePending = false;

            const bool bToggleChanged = _bEnableShadowMapping != _pendingEnableShadowMapping ||
                                        _bEnablePointLightShadow != _pendingEnablePointLightShadow;

            _maxPointLightShadowCount = _pendingMaxPointLightShadowCount;
            if (bToggleChanged) {
                applyShadowSettings(_pendingEnableShadowMapping, _pendingEnablePointLightShadow);
                return;
            }

            syncShadowSettings();
        });
}

void DeferredRenderPipeline::applyShadowSettings(bool bEnableShadowMapping, bool bEnablePointLightShadow)
{
    const bool bChanged = _bEnableShadowMapping != bEnableShadowMapping ||
                          _bEnablePointLightShadow != bEnablePointLightShadow;
    if (!bChanged) {
        return;
    }

    if (_render) {
        _render->waitIdle();
    }

    _bEnableShadowMapping    = bEnableShadowMapping;
    _bEnablePointLightShadow = bEnablePointLightShadow;

    // Sync App-layer settings
    auto& shadowSettings = App::get()->getShadowSettings();
    if (!bEnableShadowMapping) {
        shadowSettings.quality = EShadowQuality::Off;
    }
    else if (shadowSettings.quality == EShadowQuality::Off) {
        shadowSettings = ShadowSettings::fromQuality(EShadowQuality::Medium);
    }
    shadowSettings.pointLightEnabled = bEnablePointLightShadow;

    if (_bEnableShadowMapping) {
        if (!_shadowDepthRT) {
            initShadowResources();
        }
        if (!_shadowStage && _shadowDepthRT) {
            _shadowStage = ya::makeShared<ShadowStage>();
            _shadowStage->setRenderTarget(_shadowDepthRT);
            _shadowStage->init(_render);
        }
    }
    else {
        destroyShadowResources();
    }

    syncShadowSettings();
}

void DeferredRenderPipeline::loadPersistentSettings()
{
    auto& cfgManager = ConfigManager::get();

    _bEnableShadowMapping        = cfgManager.getOr<bool>(DEFERRED_PIPELINE_CONFIG_DOC_NAME,
                                                   DEFERRED_PIPELINE_CONFIG_KEY_ENABLE_SHADOW_MAPPING,
                                                   _bEnableShadowMapping);
    _bEnablePointLightShadow     = cfgManager.getOr<bool>(DEFERRED_PIPELINE_CONFIG_DOC_NAME,
                                                      DEFERRED_PIPELINE_CONFIG_KEY_ENABLE_POINT_LIGHT_SHADOW,
                                                      _bEnablePointLightShadow);
    int maxPointLightShadowCount = cfgManager.getOr<int>(DEFERRED_PIPELINE_CONFIG_DOC_NAME,
                                                         DEFERRED_PIPELINE_CONFIG_KEY_MAX_POINT_LIGHT_SHADOWS,
                                                         static_cast<int>(_maxPointLightShadowCount));

    _maxPointLightShadowCount        = static_cast<uint32_t>(std::clamp(maxPointLightShadowCount, 0, static_cast<int>(MAX_POINT_LIGHTS)));
    _pendingEnableShadowMapping      = _bEnableShadowMapping;
    _pendingEnablePointLightShadow   = _bEnablePointLightShadow;
    _pendingMaxPointLightShadowCount = _maxPointLightShadowCount;

    // Sync loaded config to App-layer ShadowSettings
    auto& shadowSettings = App::get()->getShadowSettings();
    if (!_bEnableShadowMapping) {
        shadowSettings.quality = EShadowQuality::Off;
    }
    shadowSettings.pointLightEnabled    = _bEnablePointLightShadow;
    shadowSettings.maxPointLightShadows = _maxPointLightShadowCount;

    const auto& automationShadowOverrides = App::get()->getDesc().automation.shadow;
    if (automationShadowOverrides.quality) {
        shadowSettings = ShadowSettings::fromQuality(*automationShadowOverrides.quality);
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

    _bEnableShadowMapping      = shadowSettings.isEnabled();
    _bEnablePointLightShadow   = shadowSettings.pointLightEnabled;
    _maxPointLightShadowCount  = shadowSettings.getEffectivePointLightCount();
    _pendingEnableShadowMapping      = _bEnableShadowMapping;
    _pendingEnablePointLightShadow   = _bEnablePointLightShadow;
    _pendingMaxPointLightShadowCount = _maxPointLightShadowCount;
}

void DeferredRenderPipeline::saveShadowSettingsToConfig(bool     bEnableShadowMapping,
                                                        bool     bEnablePointLightShadow,
                                                        uint32_t maxPointLightShadowCount) const
{
    ConfigManager::Editor(DEFERRED_PIPELINE_CONFIG_DOC_NAME)
        .set(DEFERRED_PIPELINE_CONFIG_KEY_ENABLE_SHADOW_MAPPING, bEnableShadowMapping)
        .set(DEFERRED_PIPELINE_CONFIG_KEY_ENABLE_POINT_LIGHT_SHADOW, bEnablePointLightShadow)
        .set(DEFERRED_PIPELINE_CONFIG_KEY_MAX_POINT_LIGHT_SHADOWS,
             static_cast<int>(std::min(maxPointLightShadowCount, static_cast<uint32_t>(MAX_POINT_LIGHTS))));
}

void DeferredRenderPipeline::rebuildShadowViews()
{
    _shadowDirectionalDepthIV.reset();
    for (auto& imageView : _shadowPointCubeIVs) {
        imageView.reset();
    }
    for (auto& faceViews : _shadowPointFaceIVs) {
        for (auto& imageView : faceViews) {
            imageView.reset();
        }
    }

    auto* textureFactory    = _render->getTextureFactory();
    auto* shadowFrameBuffer = _shadowDepthRT->getCurFrameBuffer();
    YA_CORE_ASSERT(shadowFrameBuffer, "Deferred shadow resources require a valid framebuffer");

    auto* shadowDepthTexture = shadowFrameBuffer->getDepthTexture();
    YA_CORE_ASSERT(shadowDepthTexture, "Deferred shadow resources require a valid depth texture");

    auto shadowImage = shadowDepthTexture->getImageShared();
    YA_CORE_ASSERT(textureFactory && shadowImage, "Deferred shadow resources require a valid image");

    _shadowDirectionalDepthIV = textureFactory->createImageView(
        shadowImage,
        ImageViewCreateInfo{
            .label          = "Deferred Shadow Directional Depth IV",
            .viewType       = EImageViewType::View2D,
            .aspectFlags    = EImageAspect::Depth,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        });

    for (uint32_t lightIndex = 0; lightIndex < MAX_POINT_LIGHTS; ++lightIndex) {
        _shadowPointCubeIVs[lightIndex] = textureFactory->createImageView(
            shadowImage,
            ImageViewCreateInfo{
                .label          = std::format("Deferred Shadow Point[{}] CubeIV", lightIndex),
                .viewType       = EImageViewType::ViewCube,
                .aspectFlags    = EImageAspect::Depth,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 1 + lightIndex * 6,
                .layerCount     = 6,
            });

        for (uint32_t faceIndex = 0; faceIndex < 6; ++faceIndex) {
            _shadowPointFaceIVs[lightIndex][faceIndex] = textureFactory->createImageView(
                shadowImage,
                ImageViewCreateInfo{
                    .label          = std::format("Deferred Shadow Point[{}] Face[{}]", lightIndex, faceIndex),
                    .viewType       = EImageViewType::View2D,
                    .aspectFlags    = EImageAspect::Depth,
                    .baseMipLevel   = 0,
                    .levelCount     = 1,
                    .baseArrayLayer = 1 + lightIndex * 6 + faceIndex,
                    .layerCount     = 1,
                });
        }
    }
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
    if (_bEnableShadowMapping) {
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
    if (_shadowDepthRT) {
        _shadowStage = ya::makeShared<ShadowStage>();
        _shadowStage->setRenderTarget(_shadowDepthRT);
        _shadowStage->init(_render);
    }

    _gBufferStage = ya::makeShared<GBufferStage>();
    _gBufferStage->init(_render);

    _lightStage = ya::makeShared<LightStage>();
    _lightStage->setup(_gBufferStage.get(), _gBufferRT.get());
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
    if (_gBufferStage) {
        _gBufferStage->destroy();
        _gBufferStage.reset();
    }

    _viewportRT.reset();
    _gBufferRT.reset();
    destroyShadowResources();
    _bShadowSettingsChangePending = false;
    _render                       = nullptr;
}


// ═══════════════════════════════════════════════════════════════════════
// Tick
// ═══════════════════════════════════════════════════════════════════════

void DeferredRenderPipeline::tick(const TickDesc& desc)
{
    desc.cmdBuf->debugBeginLabel("Deferred Pipeline");

    if (shouldSkipTick(desc)) {
        return;
    }

    YA_PERF_SCOPE(perf::sample::deferredTick(), perf::metric::cpuTimeMs(), perf::domain::render());

    RenderStageContext stageCtx{};
    uint32_t           vpW = 0;
    uint32_t           vpH = 0;
    beginTick(desc, stageCtx, vpW, vpH);
    refreshDirtyResources();
    syncFrameSettings(desc);
    executeShadowPass(stageCtx);
    executeGBufferPass(desc, stageCtx, vpW, vpH);
    executeDepthCopyPass(desc.cmdBuf);

    beginViewportRendering(desc);
    executeViewportPass(desc, stageCtx);

    desc.cmdBuf->debugEndLabel();
}

bool DeferredRenderPipeline::shouldSkipTick(const TickDesc& desc) const
{
    YA_CORE_ASSERT(desc.cmdBuf, "DeferredRenderPipeline requires a command buffer");

    if (desc.viewportRect.extent.x <= 0 || desc.viewportRect.extent.y <= 0) {
        desc.cmdBuf->debugEndLabel();
        return true;
    }

    if (!desc.frameData) {
        desc.cmdBuf->debugEndLabel();
        return true;
    }

    return false;
}

void DeferredRenderPipeline::beginTick(const TickDesc& desc, RenderStageContext& stageCtx, uint32_t& vpW, uint32_t& vpH)
{
    _postProcessStage.beginFrame();

    vpW = static_cast<uint32_t>(desc.viewportRect.extent.x);
    vpH = static_cast<uint32_t>(desc.viewportRect.extent.y);

    _lastPointLightCount = desc.frameData->numPointLights;
    _lastDrawCount       = static_cast<uint32_t>(desc.frameData->totalDrawCount());

    stageCtx = RenderStageContext{
        .cmdBuf         = desc.cmdBuf,
        .frameData      = desc.frameData,
        .flightIndex    = desc.flightIndex,
        .deltaTime      = desc.dt,
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
    const bool bShadowResourcesDirty = _shadowDepthRT && _shadowDepthRT->bDirty;
    const bool bShadowPipelineDirty  = _shadowDepthRT && _shadowDepthRT->hasDirtyReason(ERenderTargetDirtyReason::Attachments);
    if (_shadowDepthRT) {
        _shadowDepthRT->flushDirty();
    }

    if (bGBufferDirty) {
        _cachedAlbedoSpecImageViewHandle = nullptr;
        _debugAlbedoRGBView.reset();
        _debugSpecularAlphaView.reset();
        if (_lightStage) {
            _lightStage->invalidateGBufferDescriptors();
        }
        if (bGBufferPipelineDirty && _gBufferStage) {
            _gBufferStage->refreshPipelineFormats(_gBufferRT.get());
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

void DeferredRenderPipeline::syncFrameSettings(const TickDesc& desc)
{
    (void)desc;

    const auto&    shadowSettings           = App::get()->getShadowSettings();
    const uint32_t shadowedPointLightBudget = shadowSettings.getEffectivePointLightCount();
    if (_gBufferStage) {
        _gBufferStage->setMaxShadowedPointLights(shadowedPointLightBudget);
    }
    if (_lightStage) {
        _lightStage->setShadowSettings(shadowSettings.isEnabled(), shadowSettings.pointLightEnabled);
    }
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
    auto* shadowFrameBuffer  = _shadowDepthRT ? _shadowDepthRT->getCurFrameBuffer() : nullptr;
    auto* shadowDepthTexture = shadowFrameBuffer ? shadowFrameBuffer->getDepthTexture() : nullptr;
    auto* shadowDepthImage   = shadowDepthTexture ? shadowDepthTexture->getImage() : nullptr;
    if (!cmdBuf || !_shadowDepthRT || !shadowDepthImage) {
        return;
    }

    ImageSubresourceRange shadowDepthRange{
        .aspectMask     = EImageAspect::Depth,
        .baseMipLevel   = 0,
        .levelCount     = 1,
        .baseArrayLayer = 0,
        .layerCount     = _shadowDepthRT->_layerCount,
    };
    cmdBuf->transitionImageLayoutAuto(shadowDepthImage, EImageLayout::ShaderReadOnlyOptimal, &shadowDepthRange);
}

void DeferredRenderPipeline::executeGBufferPass(const TickDesc& desc, const RenderStageContext& stageCtx, uint32_t vpW, uint32_t vpH)
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
    desc.cmdBuf->beginRendering(gBufferRI);

    float gbVpY = 0.0f;
    float gbVpH = static_cast<float>(vpH);
    if (_bReverseViewportY) {
        gbVpY = static_cast<float>(vpH);
        gbVpH = -gbVpH;
    }
    desc.cmdBuf->setViewport(0.0f, gbVpY, static_cast<float>(vpW), gbVpH);
    desc.cmdBuf->setScissor(0, 0, vpW, vpH);

    _gBufferStage->execute(stageCtx);

    desc.cmdBuf->endRendering(gBufferRI);
}

void DeferredRenderPipeline::executeDepthCopyPass(ICommandBuffer* cmdBuf)
{
    YA_PERF_SCOPE(perf::sample::deferredDepthCopy(), perf::metric::cpuTimeMs(), perf::domain::render());
    copyGBufferDepthToViewport(cmdBuf);
}

void DeferredRenderPipeline::executeViewportPass(const TickDesc& desc, RenderStageContext& stageCtx)
{

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

void DeferredRenderPipeline::beginViewportRendering(const TickDesc& desc)
{
    auto cmdBuf = desc.cmdBuf;

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

    const uint32_t vpW = static_cast<uint32_t>(desc.viewportRect.extent.x);
    const uint32_t vpH = static_cast<uint32_t>(desc.viewportRect.extent.y);

    _lastTickCtx = {
        .view       = desc.view,
        .projection = desc.projection,
        .cameraPos  = desc.cameraPos,
        .extent     = {.width = vpW, .height = vpH},
    };
    _lastTickDesc = desc;
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
            cmdBuf, inputTexture, _lastTickDesc.viewportRect.extent, &_lastTickCtx);
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

    _cachedAlbedoSpecImageViewHandle = nullptr;
    _debugAlbedoRGBView.reset();
    _debugSpecularAlphaView.reset();
    _postProcessStage.onViewportResized(newExtent);
}

// ═══════════════════════════════════════════════════════════════════════
// GUI
// ═══════════════════════════════════════════════════════════════════════

void DeferredRenderPipeline::renderGUI(bool bRenderTreeNode)
{

    if (bRenderTreeNode && !ImGui::TreeNode("Deferred Pipeline")) return;

    if (ImGui::TreeNode("Settings")) {
        ImGui::Checkbox("GBuffer Reverse Viewport Y", &_bReverseViewportY);
        bool bEnablePerfStats = YA_PERF_IS_ENABLED();
        if (ImGui::Checkbox("Enable Perf Stats", &bEnablePerfStats)) {
            YA_PERF_SET_ENABLED(bEnablePerfStats);
        }
        ImGui::TextUnformatted("GBuffer ID + switch/case Light Pass");

        if (ImGui::TreeNode("Shadow")) {
            auto& shadowSettings = App::get()->getShadowSettings();

            bool bShadowEnabled = _bEnableShadowMapping;
            if (ImGui::Checkbox("Enable Shadow Mapping", &bShadowEnabled)) {
                queueShadowSettingsChange(bShadowEnabled, _bEnablePointLightShadow, _maxPointLightShadowCount);
            }

            if (_bEnableShadowMapping && _shadowStage) {
                static const char* qualityNames[] = {"Low", "Medium", "High", "Ultra"};
                int                qualityIdx     = std::max(0, static_cast<int>(shadowSettings.quality) - 1);
                if (ImGui::Combo("Quality Preset", &qualityIdx, qualityNames, IM_ARRAYSIZE(qualityNames))) {
                    auto newQuality = static_cast<EShadowQuality::T>(qualityIdx + 1);
                    shadowSettings  = ShadowSettings::fromQuality(newQuality);
                }

                ImGui::Checkbox("Directional Shadow", &shadowSettings.directionalEnabled);
                ImGui::Checkbox("Point Light Shadow", &shadowSettings.pointLightEnabled);
                ImGui::Checkbox("Point Light Indirect Draw", &shadowSettings.pointLightUseIndirect);
                ImGui::Checkbox("Point Light Indirect Cull", &shadowSettings.pointLightIndirectCullEnabled);
                int maxPL = static_cast<int>(shadowSettings.maxPointLightShadows);
                if (ImGui::SliderInt("Max Point Shadows", &maxPL, 0, MAX_POINT_LIGHTS)) {
                    shadowSettings.maxPointLightShadows = static_cast<uint32_t>(maxPL);
                }
                ImGui::DragFloat("Depth Bias", &shadowSettings.bias, 0.0001f, 0.0f, 0.1f, "%.5f");
                ImGui::DragFloat("Normal Bias", &shadowSettings.normalBias, 0.0001f, 0.0f, 0.1f, "%.5f");

                static const char* filterNames[] = {"Hard", "PCF Low", "PCF High"};
                int                currentFilter = static_cast<int>(shadowSettings.filter);
                if (ImGui::Combo("Shadow Filter", &currentFilter, filterNames, IM_ARRAYSIZE(filterNames))) {
                    shadowSettings.filter = static_cast<EShadowFilter::T>(currentFilter);
                }
            }
            ImGui::TreePop();
        }
        ImGui::TreePop();
    }

    if (YA_PERF_IS_ENABLED()) {
        if (ImGui::TreeNode("Perf")) {
            auto& perf = PerfState::Get();

            static constexpr size_t WINDOW_SIZES[]       = {1, 10, 30, 60};
            static constexpr const char* WINDOW_LABELS[] = {"Last", "10 frames", "30 frames", "60 frames"};
            int currentWindowIndex = 0;
            for (int i = 0; i < IM_ARRAYSIZE(WINDOW_SIZES); ++i) {
                if (perf.getAverageWindowSize() == WINDOW_SIZES[i]) {
                    currentWindowIndex = i;
                    break;
                }
            }
            if (ImGui::Combo("Average", &currentWindowIndex, WINDOW_LABELS, IM_ARRAYSIZE(WINDOW_LABELS))) {
                perf.setAverageWindowSize(WINDOW_SIZES[currentWindowIndex]);
            }

            auto metric = [&perf](FName sampleKey, FName metricKey) {
                return perf.getDisplayValue(sampleKey, metricKey);
            };
            auto cpu = [&metric](FName sampleKey) {
                return metric(sampleKey, perf::metric::cpuTimeMs());
            };

            const float frameCpuMs      = cpu(perf::sample::renderFrame());
            const float frameGpuMs      = metric(perf::sample::renderFrame(), perf::metric::gpuTimeMs());
            const float logicMs         = cpu(perf::sample::frameLogic());
            const float renderMs        = cpu(perf::sample::frameRender());
            const float automationMs    = cpu(perf::sample::frameAutomation());
            const float unaccountedMs   = cpu(perf::sample::frameUnaccounted());
            const float extractMs       = cpu(perf::sample::renderExtract());
            const float runtimeMs       = cpu(perf::sample::renderRuntime());
            const float prepareFrameMs  = cpu(perf::sample::renderPrepareFrame());
            const float waitIdleMs      = cpu(perf::sample::renderWaitIdle());
            const float beginMs         = cpu(perf::sample::renderBegin());
            const float waitFenceMs     = cpu(perf::sample::vulkanWaitFence());
            const float acquireMs       = cpu(perf::sample::vulkanAcquire());
            const float worldMs         = cpu(perf::sample::renderWorld());
            const float deferredTickMs  = cpu(perf::sample::deferredTick());
            const float shadowMs        = cpu(perf::sample::deferredShadow());
            const float gbufferMs       = cpu(perf::sample::deferredGBuffer());
            const float depthCopyMs     = cpu(perf::sample::deferredDepthCopy());
            const float lightMs         = cpu(perf::sample::deferredLight());
            const float overlayMs       = cpu(perf::sample::deferredOverlay());
            const float viewportOverlayMs = cpu(perf::sample::renderViewportOverlay());
            const float postProcessMs   = cpu(perf::sample::renderPostProcess());
            const float presentationMs  = cpu(perf::sample::renderPresentation());
            const float flushCallbacksMs = cpu(perf::sample::renderFlushCallbacks());
            const float submitMs        = cpu(perf::sample::renderSubmit());
            const float presentMs       = cpu(perf::sample::vulkanPresent());

            ImGui::Text("CPU frame: %.3f ms", frameCpuMs);
            ImGui::Text("GPU frame: %.3f ms", frameGpuMs);

            drawPerfNode("Frame Cycle", frameCpuMs, [&]() {
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
            });

            ImGui::Text("Draw items: %u", _lastDrawCount);
            ImGui::Text("Point lights: %u", _lastPointLightCount);
            ImGui::TreePop();
        }
    }

    if (ImGui::TreeNode("Stages")) {
        if (_shadowStage) _shadowStage->renderGUI();
        if (_gBufferStage) _gBufferStage->renderGUI();
        if (_lightStage) _lightStage->renderGUI();
        if (_overlayStage) _overlayStage->renderGUI();
        _postProcessStage.renderGUI();
        ImGui::TreePop();
    }

    if (bRenderTreeNode) { ImGui::TreePop(); }
}

} // namespace ya
