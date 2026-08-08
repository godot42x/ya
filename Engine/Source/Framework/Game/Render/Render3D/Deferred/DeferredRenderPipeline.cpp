#include "DeferredRenderPipeline.h"

#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Core/Profiling/Profiling.h"
#include "DeferredViewportResources.h"
#include "DeferredAttachmentFormats.h"
#include "ECS/Component/2D/BillboardComponent.h"
#include "ECS/Component/3D/SkyboxComponent.h"
#include "ECS/Component/DirectionComponent.h"
#include "ECS/Component/Mesh/StaticMeshComponent.h"
#include "Scene3D/TransformComponent.h"
#include "GUI/Runtime/Resource/TextureSlotBinding.h"
#include "Render3D/EnvironmentLighting/EnvironmentLightingProcessor.h"
#include "RHI/Core/Sampler.h"
#include "Graph/RenderGraphImportUtils.h"
#include "RHI/Core/RenderImage.h"
#include "RHI/Core/Swapchain.h"
#include "RHI/Core/Texture.h"
#include "Resource/Mesh/PrimitiveMeshCache.h"
#include "Host/App.h"
#include "Render3D/Common/Shadow/Common/ShadowSettingsConfig.h"
#include "Graph/RenderGraphExecutor.h"
#include "Host/Config/ConfigManager.h"

#include "Scene/Core/Scene.h"
#include <algorithm>
#include <chrono>
#include <format>

namespace ya
{

namespace
{

bool shouldRenderBillboard(const BillboardComponent& billboard)
{
    if (!billboard.bVisible) {
        return false;
    }

    if (!billboard.bManagedByLight) {
        return true;
    }

    if (const auto* app = App::get()) {
        return app->isStopped();
    }

    return false;
}

EFormat::T chooseSupportedAttachmentFormat(IRender* render,
                                           std::string_view label,
                                           EImageUsage::T usage,
                                           std::initializer_list<EFormat::T> candidates,
                                           EImageCreateFlag::T flags = EImageCreateFlag::None,
                                           ESampleCount::T samples = ESampleCount::Sample_1)
{
    YA_CORE_ASSERT(render, "chooseSupportedAttachmentFormat requires render backend");
    for (const auto format : candidates) {
        if (render->isImageFormatSupported(format, usage, flags, samples)) {
            return format;
        }
    }

    const auto preferred = candidates.begin();
    YA_CORE_WARN("No supported attachment format found for '{}', keeping preferred format {}", label, preferred != candidates.end() ? std::to_string(*preferred) : "Undefined");
    return preferred != candidates.end() ? *preferred : EFormat::Undefined;
}

ViewportOverlayStage::FrameInputs::DirectionGizmoInput buildDirectionGizmoInput(const TransformComponent& tc)
{
    const glm::mat4 worldTransform = glm::translate(glm::mat4(1.0f), tc.getWorldPosition()) *
                                     glm::mat4_cast(glm::quat(glm::radians(tc.getRotation())));
    const glm::mat4 coneLocalTransf =
        glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1, 0, 0)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(0.3f, 1.0f, 0.3f));
    const glm::mat4 cylinderLocalTransf =
        glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1, 0, 0)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(0.1f, 1.0f, 0.1f));

    return ViewportOverlayStage::FrameInputs::DirectionGizmoInput{
        .coneModel     = glm::translate(glm::mat4(1.0f), -tc.getForward()) * coneLocalTransf * worldTransform,
        .cylinderModel = worldTransform * cylinderLocalTransf,
        .lineStart     = tc.getWorldPosition(),
        .lineEnd       = tc.getWorldPosition() + tc.getForward(),
    };
}

DeferredAttachmentFormats buildDeferredGBufferFormats(EFormat::T signedLinearFormat,
                                                      EFormat::T linearFormat,
                                                      EFormat::T shadingModelFormat,
                                                      EFormat::T depthFormat);
DeferredAttachmentFormats buildDeferredViewportFormats(EFormat::T colorFormat, EFormat::T depthFormat);
RenderTargetCreateInfo buildDeferredGBufferRenderTargetSpec(Extent2D extent,
                                                            EFormat::T signedLinearFormat,
                                                            EFormat::T linearFormat,
                                                            EFormat::T shadingModelFormat,
                                                            EFormat::T depthFormat);
RenderTargetCreateInfo buildDeferredViewportRenderTargetSpec(Extent2D extent, EFormat::T colorFormat);
DeferredAttachmentFormats buildDeferredFormatsFromSpec(const RenderTargetCreateInfo& spec);

RGImportedTextureDesc makeDeferredEnvironmentImportedDesc(const ImageResourceRef& resource,
                                                          std::string_view                    label)
{
    return makeImportedTextureDesc(resource, label, EImageLayout::ShaderReadOnlyOptimal);
}

RGTextureDesc makeGraphAttachmentDesc(const RenderTargetCreateInfo& spec,
                                      const AttachmentDescription&  attachment,
                                      std::string                    label)
{
    return RGTextureDesc{
        .label       = std::move(label),
        .format      = attachment.format,
        .extent      = Extent3D{spec.extent.width, spec.extent.height, 1},
        .mipLevels   = 1,
        .arrayLayers = spec.layerCount,
        .samples     = attachment.samples,
        .usage       = attachment.usage,
        .flags       = attachment.imageCreateFlags,
    };
}

DeferredAttachmentFormats buildDeferredGBufferFormats(EFormat::T signedLinearFormat,
                                                      EFormat::T linearFormat,
                                                      EFormat::T shadingModelFormat,
                                                      EFormat::T depthFormat)
{
    DeferredAttachmentFormats formats{};
    formats.colorFormats = {
        signedLinearFormat,
        signedLinearFormat,
        linearFormat,
        shadingModelFormat,
    };
    formats.depthFormat = depthFormat;
    return formats;
}

DeferredAttachmentFormats buildDeferredViewportFormats(EFormat::T colorFormat, EFormat::T depthFormat)
{
    DeferredAttachmentFormats formats{};
    formats.colorFormats = {colorFormat};
    formats.depthFormat  = depthFormat;
    return formats;
}

RenderTargetCreateInfo buildDeferredGBufferRenderTargetSpec(Extent2D extent,
                                                            EFormat::T signedLinearFormat,
                                                            EFormat::T linearFormat,
                                                            EFormat::T shadingModelFormat,
                                                            EFormat::T depthFormat)
{
    return RenderTargetCreateInfo{
        .label            = "GBuffer RenderTarget",
        .renderingMode    = ERenderingMode::DynamicRendering,
        .bSwapChainTarget = false,
        .extent           = extent,
        .frameBufferCount = 1,
        .attachments      = {
            .colorAttach = {
                AttachmentDescription{
                    .index         = 0,
                    .format        = signedLinearFormat,
                    .initialLayout = EImageLayout::ColorAttachmentOptimal,
                    .finalLayout   = EImageLayout::ShaderReadOnlyOptimal,
                    .usage         = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                },
                AttachmentDescription{
                    .index         = 1,
                    .format        = signedLinearFormat,
                    .initialLayout = EImageLayout::ColorAttachmentOptimal,
                    .finalLayout   = EImageLayout::ShaderReadOnlyOptimal,
                    .usage         = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                },
                AttachmentDescription{
                    .index         = 2,
                    .format        = linearFormat,
                    .initialLayout = EImageLayout::ColorAttachmentOptimal,
                    .finalLayout   = EImageLayout::ShaderReadOnlyOptimal,
                    .usage         = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                },
                AttachmentDescription{
                    .index         = 3,
                    .format        = shadingModelFormat,
                    .initialLayout = EImageLayout::ColorAttachmentOptimal,
                    .finalLayout   = EImageLayout::ShaderReadOnlyOptimal,
                    .usage         = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                },
            },
            .depthAttach = AttachmentDescription{
                .index          = 4,
                .format         = depthFormat,
                .loadOp         = EAttachmentLoadOp::Clear,
                .storeOp        = EAttachmentStoreOp::Store,
                .stencilLoadOp  = EAttachmentLoadOp::Clear,
                .stencilStoreOp = EAttachmentStoreOp::Store,
                .initialLayout  = EImageLayout::DepthStencilAttachmentOptimal,
                .finalLayout    = EImageLayout::ShaderReadOnlyOptimal,
                .usage          = EImageUsage::DepthStencilAttachment | EImageUsage::Sampled,
            },
        },
    };
}

RenderTargetCreateInfo buildDeferredViewportRenderTargetSpec(Extent2D extent, EFormat::T colorFormat)
{
    return RenderTargetCreateInfo{
        .label            = "Deferred Viewport RT",
        .bSwapChainTarget = false,
        .extent           = extent,
        .attachments      = {
            .colorAttach = {
                AttachmentDescription{
                    .index          = 0,
                    .format         = colorFormat,
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
        },
    };
}

DeferredAttachmentFormats buildDeferredFormatsFromSpec(const RenderTargetCreateInfo& spec)
{
    DeferredAttachmentFormats formats{};
    formats.colorFormats.reserve(spec.attachments.colorAttach.size());
    for (const auto& colorDesc : spec.attachments.colorAttach) {
        formats.colorFormats.push_back(colorDesc.format);
    }
    if (spec.attachments.depthAttach.has_value()) {
        formats.depthFormat = spec.attachments.depthAttach->format;
    }
    return formats;
}

} // namespace

DeferredRenderPipeline::~DeferredRenderPipeline()
{
    shutdown();
}

void DeferredRenderPipeline::initShadowResources()
{
    if (!_render || _shadowResources.depthImage) {
        return;
    }

    const auto  shadowSettings      = currentShadowSettings();
    const uint32_t shadowResolution = std::max(shadowSettings.resolution, 1u);

    _shadowResources.init(_render, ShadowMapResourceDesc{
        .imageLabel        = "Deferred Shadow Depth",
        .samplerLabel      = "deferred-shadow",
        .viewLabelPrefix   = "Deferred Shadow",
        .extent            = {.width = shadowResolution, .height = shadowResolution},
        .depthFormat       = _shadowDepthFormat,
    });
}

void DeferredRenderPipeline::resolveRuntimeFormats()
{
    constexpr auto sampledColorUsage = static_cast<EImageUsage::T>(EImageUsage::ColorAttachment | EImageUsage::Sampled);
    constexpr auto sampledDepthUsage = static_cast<EImageUsage::T>(EImageUsage::DepthStencilAttachment | EImageUsage::Sampled);

    _gBufferSignedLinearFormat = chooseSupportedAttachmentFormat(
        _render,
        "Deferred GBuffer HDR",
        sampledColorUsage,
        {SIGNED_LINEAR_FORMAT, EFormat::R8G8B8A8_UNORM});
    _viewportColorFormat = chooseSupportedAttachmentFormat(
        _render,
        "Deferred Viewport Color",
        static_cast<EImageUsage::T>(sampledColorUsage | EImageUsage::TransferSrc),
        {VIEWPORT_COLOR_FORMAT, EFormat::R8G8B8A8_UNORM});
    _sharedDepthFormat = chooseSupportedAttachmentFormat(
        _render,
        "Deferred Shared Depth",
        sampledDepthUsage,
        {DEPTH_FORMAT, EFormat::D32_SFLOAT_S8_UINT, EFormat::D24_UNORM_S8_UINT, EFormat::D16_UNORM});
    _shadowDepthFormat = chooseSupportedAttachmentFormat(
        _render,
        "Deferred Shadow Depth",
        sampledDepthUsage,
        {SHADOW_DEPTH_FORMAT, EFormat::D32_SFLOAT_S8_UINT, EFormat::D24_UNORM_S8_UINT, EFormat::D16_UNORM},
        EImageCreateFlag::CubeCompatible);
}

void DeferredRenderPipeline::initRenderTargetSpecs(Extent2D extent)
{
    _gBufferRTSpec = buildDeferredGBufferRenderTargetSpec(
        extent,
        _gBufferSignedLinearFormat,
        LINEAR_FORMAT,
        SHADING_MODEL_FORMAT,
        _sharedDepthFormat);
    _viewportRTSpec = buildDeferredViewportRenderTargetSpec(extent, _viewportColorFormat);
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

    if (_frameResources) {
        _frameResources->applyShadowState(shadowState);
    }
}

ShadowSettings DeferredRenderPipeline::currentShadowSettings() const
{
    return _frameShadowSettings;
}

DeferredRenderPipeline::SettingsSnapshot DeferredRenderPipeline::buildSettingsSnapshot() const
{
    return {
        .bReverseViewportY = _bReverseViewportY,
        .bSSAOEnabled      = _bEnableSSAO,
        .shadow            = currentShadowSettings(),
        .postProcessing    = _postProcessStage.getState(),
    };
}

void DeferredRenderPipeline::requestSettings(const SettingsSnapshot& settings)
{
    _pendingSettings = settings;
}

bool DeferredRenderPipeline::isShadowMappingEnabled() const
{
    return currentShadowSettings().isEnabled();
}

ShadowRuntimeState DeferredRenderPipeline::buildShadowState() const
{
    ShadowRuntimeState shadowState{};
    const ShadowSettings shadowSettings = currentShadowSettings();
    shadowState.bEnableShadowMapping    = shadowSettings.isEnabled();
    shadowState.bEnablePointLightShadow = shadowSettings.pointLightEnabled;
    shadowState.maxShadowedPointLights  = shadowSettings.getEffectivePointLightCount();
    shadowState.shadowMapResolution     = _shadowResources.extent.width > 0 ? _shadowResources.extent.width : std::max(shadowSettings.resolution, 1u);
    shadowState.filter                  = shadowSettings.filter;
    shadowState.bias                    = shadowSettings.bias;
    shadowState.normalBias              = shadowSettings.normalBias;

    if (shadowState.bEnableShadowMapping && _shadowResources.directionalDepthIV && _shadowResources.sampler) {
        shadowState.directionalDepthIV = _shadowResources.directionalDepthIV.get();
        shadowState.sampler            = _shadowResources.sampler.get();
        for (uint32_t lightIndex = 0; lightIndex < MAX_POINT_LIGHTS; ++lightIndex) {
            shadowState.pointCubeDepthIVs[lightIndex] = _shadowResources.pointCubeIVs[lightIndex].get();
        }
    }

    return shadowState;
}

void DeferredRenderPipeline::applyPendingSettings()
{
    if (!_pendingSettings) {
        return;
    }

    const SettingsSnapshot settings = std::move(*_pendingSettings);
    _pendingSettings.reset();

    _bReverseViewportY = settings.bReverseViewportY;
    setSSAOEnabled(settings.bSSAOEnabled);
    _postProcessStage.getState() = settings.postProcessing;
    applyShadowSettings(settings.shadow);
}

void DeferredRenderPipeline::applyShadowSettings(const ShadowSettings& shadowSettings)
{
    const bool bWasEnabled    = currentShadowSettings().isEnabled();
    const bool bWillEnable    = shadowSettings.isEnabled();
    const bool bToggleChanged = bWasEnabled != bWillEnable;

    _frameShadowSettings = shadowSettings;
    if (_shadowSettings) {
        *_shadowSettings = shadowSettings;
    }

    if (bToggleChanged || (shadowSettings.isEnabled() && !_shadowResources.depthImage)) {
        requestShadowResourceRefresh();
    }

    syncShadowSettings();
}

void DeferredRenderPipeline::loadPersistentSettings()
{
    constexpr const char* RUNTIME_CONFIG_DOCUMENT = "runtime";

    auto& config = ConfigManager::get();
    _bReverseViewportY = config.getOr<bool>(RUNTIME_CONFIG_DOCUMENT, "render.deferred.reverseViewportY", _bReverseViewportY);
    _bEnableSSAO       = config.getOr<bool>(RUNTIME_CONFIG_DOCUMENT, "render.deferred.ssaoEnabled", _bEnableSSAO);

    auto& post = _postProcessStage.getState();
    post.bEnableInversion       = config.getOr<bool>(RUNTIME_CONFIG_DOCUMENT, "render.postprocess.basic.inversion", post.bEnableInversion);
    post.grayscaleMode          = static_cast<PostProcessingState::EGrayscaleMode>(config.getOr<int>(RUNTIME_CONFIG_DOCUMENT, "render.postprocess.basic.grayscale", static_cast<int>(post.grayscaleMode)));
    post.kernelMode             = static_cast<PostProcessingState::EKernelMode>(config.getOr<int>(RUNTIME_CONFIG_DOCUMENT, "render.postprocess.basic.kernel", static_cast<int>(post.kernelMode)));
    post.kernelTexelOffset      = config.getOr<float>(RUNTIME_CONFIG_DOCUMENT, "render.postprocess.basic.kernelTexelOffset", post.kernelTexelOffset);
    post.bEnableToneMapping     = config.getOr<bool>(RUNTIME_CONFIG_DOCUMENT, "render.postprocess.basic.tonemapping.enabled", post.bEnableToneMapping);
    post.toneMappingCurve       = static_cast<PostProcessingState::EToneMappingCurve>(config.getOr<int>(RUNTIME_CONFIG_DOCUMENT, "render.postprocess.basic.tonemapping.curve", static_cast<int>(post.toneMappingCurve)));
    post.exposure               = config.getOr<float>(RUNTIME_CONFIG_DOCUMENT, "render.postprocess.basic.tonemapping.exposure", post.exposure);
    post.bEnableGammaCorrection = config.getOr<bool>(RUNTIME_CONFIG_DOCUMENT, "render.postprocess.basic.output.gammaCorrection", post.bEnableGammaCorrection);
    post.gamma                  = config.getOr<float>(RUNTIME_CONFIG_DOCUMENT, "render.postprocess.basic.output.gamma", post.gamma);
    post.bEnableRandomGrain     = config.getOr<bool>(RUNTIME_CONFIG_DOCUMENT, "render.postprocess.basic.output.randomGrain", post.bEnableRandomGrain);
    post.randomGrainStrength    = config.getOr<float>(RUNTIME_CONFIG_DOCUMENT, "render.postprocess.basic.output.randomGrainStrength", post.randomGrainStrength);
    post.bEnableBloom           = config.getOr<bool>(RUNTIME_CONFIG_DOCUMENT, "render.postprocess.bloom.enabled", post.bEnableBloom);
    post.bloomThreshold         = config.getOr<float>(RUNTIME_CONFIG_DOCUMENT, "render.postprocess.bloom.threshold", post.bloomThreshold);
    post.bloomSoftKnee          = config.getOr<float>(RUNTIME_CONFIG_DOCUMENT, "render.postprocess.bloom.softKnee", post.bloomSoftKnee);
    post.bloomExtractIntensity  = config.getOr<float>(RUNTIME_CONFIG_DOCUMENT, "render.postprocess.bloom.extractIntensity", post.bloomExtractIntensity);
    post.bloomBlurPasses        = static_cast<uint32_t>(std::max(1, config.getOr<int>(RUNTIME_CONFIG_DOCUMENT, "render.postprocess.bloom.blurPasses", static_cast<int>(post.bloomBlurPasses))));
    post.bloomStrength          = config.getOr<float>(RUNTIME_CONFIG_DOCUMENT, "render.postprocess.bloom.strength", post.bloomStrength);

    const ShadowSettings baselineShadowSettings = _shadowSettings ? *_shadowSettings : currentShadowSettings();
    ShadowSettings shadowSettings = shadow_settings::loadRuntimeSettings(baselineShadowSettings);
    if (_automationShadowOverrides) {
        shadow_settings::applyAutomationOverrides(*_automationShadowOverrides, shadowSettings);
    }
    if (_shadowSettings) {
        *_shadowSettings = shadowSettings;
    }
    _frameShadowSettings = shadowSettings;
}

DeferredPipelineDebugViews DeferredRenderPipeline::buildDebugViews() const
{
    return DeferredPipelineDebugViews{
        .gBufferResources  = _currentGBufferResources,
        .viewportResources = _currentViewportResources,
        .ssaoTextureOwner  = _publishedGraphOutputs.ssao,
    };
}

void DeferredRenderPipeline::appendRenderTargetEntries(RenderTargetCatalog& catalog) const
{
    const DeferredAttachmentFormats gbufferFormats  = buildGBufferSnapshotFormats();
    const DeferredAttachmentFormats viewportFormats = buildViewportSnapshotFormats();
    catalog.entries.push_back({
        .label        = "Deferred GBuffer",
        .owner        = RenderTargetCatalog::Entry::EOwner::DeferredGBuffer,
        .colorFormats = gbufferFormats.colorFormats,
        .depthFormat  = gbufferFormats.depthFormat,
        .colorAttachments = {
            _currentGBufferResources.colorOwners[0],
            _currentGBufferResources.colorOwners[1],
            _currentGBufferResources.colorOwners[2],
            _currentGBufferResources.colorOwners[3],
        },
        .depthAttachment = _currentGBufferResources.depthOwner,
        .extent           = _gBufferRTSpec.extent,
        .frameBufferCount = 1,
    });
    catalog.entries.push_back({
        .label        = "Deferred Viewport",
        .owner        = RenderTargetCatalog::Entry::EOwner::DeferredViewport,
        .colorFormats = viewportFormats.colorFormats,
        .depthFormat  = viewportFormats.depthFormat,
        .colorAttachments = {_currentViewportResources.colorOwner},
        .depthAttachment  = _currentViewportResources.depthOwner,
        .extent           = _viewportRTSpec.extent,
        .frameBufferCount = 1,
    });
    catalog.entries.push_back({
        .label               = "Deferred Shadow",
        .owner               = RenderTargetCatalog::Entry::EOwner::DeferredShadow,
        .depthFormat         = _shadowResources.depthFormat,
        .depthAttachmentView = _shadowResources.directionalDepthIV,
        .extent              = _shadowResources.extent,
        .frameBufferCount    = 1,
    });
}

void DeferredRenderPipeline::setDeferredSharedDepthFormat(EFormat::T format)
{
    bool bDepthFormatChanged = false;
    if (_gBufferRTSpec.attachments.depthAttach.has_value() && _gBufferRTSpec.attachments.depthAttach->format != format) {
        _gBufferRTSpec.attachments.depthAttach->format = format;
        bDepthFormatChanged                            = true;
    }
    if (bDepthFormatChanged) {
        _sharedDepthFormat = format;
        markPendingResourceRefresh(EDeferredPendingResourceRefresh::SharedDepth);
    }
}

bool DeferredRenderPipeline::setRenderTargetDepthFormat(
    RenderTargetCatalog::Entry::EOwner owner,
    EFormat::T format)
{
    switch (owner) {
    case RenderTargetCatalog::Entry::EOwner::DeferredGBuffer:
    case RenderTargetCatalog::Entry::EOwner::DeferredViewport:
        setDeferredSharedDepthFormat(format);
        return true;
    case RenderTargetCatalog::Entry::EOwner::DeferredShadow:
        if (_shadowDepthFormat != format) {
            _shadowDepthFormat = format;
            requestShadowResourceRefresh();
        }
        return true;
    default:
        return false;
    }
}

bool DeferredRenderPipeline::setRenderTargetColorFormat(RenderTargetCatalog::Entry::EOwner owner,
                                                        uint32_t attachmentIndex,
                                                        EFormat::T format)
{
    bool bFormatChanged = false;
    switch (owner) {
    case RenderTargetCatalog::Entry::EOwner::DeferredGBuffer:
        if (attachmentIndex >= _gBufferRTSpec.attachments.colorAttach.size()) {
            return false;
        }
        if (_gBufferRTSpec.attachments.colorAttach[attachmentIndex].format != format) {
            _gBufferRTSpec.attachments.colorAttach[attachmentIndex].format = format;
            bFormatChanged                                                 = true;
        }
        if (bFormatChanged) {
            markPendingResourceRefresh(EDeferredPendingResourceRefresh::GBufferAttachments);
        }
        break;
    case RenderTargetCatalog::Entry::EOwner::DeferredViewport:
        if (attachmentIndex >= _viewportRTSpec.attachments.colorAttach.size()) {
            return false;
        }
        if (_viewportRTSpec.attachments.colorAttach[attachmentIndex].format != format) {
            _viewportRTSpec.attachments.colorAttach[attachmentIndex].format = format;
            bFormatChanged                                                  = true;
        }
        if (bFormatChanged) {
            _viewportColorFormat = _viewportRTSpec.attachments.colorAttach[0].format;
            markPendingResourceRefresh(EDeferredPendingResourceRefresh::ViewportAttachments);
        }
        break;
    default:
        return false;
    }
    return true;
}

void DeferredRenderPipeline::markPendingResourceRefresh(EDeferredPendingResourceRefresh refresh)
{
    _pendingResourceRefreshMask |= static_cast<uint32_t>(refresh);
}

bool DeferredRenderPipeline::hasPendingResourceRefresh(EDeferredPendingResourceRefresh refresh) const
{
    return (_pendingResourceRefreshMask & static_cast<uint32_t>(refresh)) != 0;
}

void DeferredRenderPipeline::clearPendingResourceRefresh(EDeferredPendingResourceRefresh refresh)
{
    _pendingResourceRefreshMask &= ~static_cast<uint32_t>(refresh);
}

void DeferredRenderPipeline::requestViewportResize(Extent2D extent)
{
    if (extent.width == 0 || extent.height == 0) {
        return;
    }

    _pendingViewportExtent  = extent;
    markPendingResourceRefresh(EDeferredPendingResourceRefresh::ViewportResize);
}

void DeferredRenderPipeline::requestShadowResourceRefresh()
{
    markPendingResourceRefresh(EDeferredPendingResourceRefresh::ShadowResources);
}

void DeferredRenderPipeline::applyPendingResourceRefreshes()
{
    if (hasPendingResourceRefresh(EDeferredPendingResourceRefresh::ViewportResize)) {
        _gBufferRTSpec.extent  = _pendingViewportExtent;
        _viewportRTSpec.extent = _pendingViewportExtent;
        clearPendingResourceRefresh(EDeferredPendingResourceRefresh::ViewportResize);
    }

    if (hasPendingResourceRefresh(EDeferredPendingResourceRefresh::ShadowResources) && _render) {
        const auto shadowSettings = currentShadowSettings();
        destroyShadowResources();

        if (shadowSettings.isEnabled()) {
            initShadowResources();
            if (!_shadowStage && _shadowResources.depthImage) {
                _shadowStage = ya::makeShared<ShadowStage>();
                _shadowStage->init(_render);
            }
            if (_shadowStage && _shadowResources.depthImage) {
                _shadowStage->refreshShadowResources(_shadowResources.depthImage, _shadowResources.depthFormat, _shadowResources.extent);
            }
        }

        clearPendingResourceRefresh(EDeferredPendingResourceRefresh::ShadowResources);
        syncShadowSettings();
    }

    if (hasPendingResourceRefresh(EDeferredPendingResourceRefresh::SharedDepth)) {
        clearPendingResourceRefresh(EDeferredPendingResourceRefresh::SharedDepth);
    }

    if (hasPendingResourceRefresh(EDeferredPendingResourceRefresh::GBufferAttachments)) {
        clearPendingResourceRefresh(EDeferredPendingResourceRefresh::GBufferAttachments);
    }

    if (hasPendingResourceRefresh(EDeferredPendingResourceRefresh::ViewportAttachments)) {
        clearPendingResourceRefresh(EDeferredPendingResourceRefresh::ViewportAttachments);
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
}

void DeferredRenderPipeline::initPipelineState(const InitDesc& desc)
{
    _render                       = desc.render;
    _graphExecutor                = _render ? std::make_unique<RenderGraphExecutor>(*_render->getResourceFactory()) : nullptr;
    _shadowSettings               = desc.shadowSettings;
    _automationShadowOverrides    = desc.automationShadowOverrides;
    _environmentLightingDSL       = desc.environmentLightingDSL;
    _runtimeServices              = desc.runtimeServices;
    _pendingSettings.reset();
    _pendingResourceRefreshMask   = 0;
    clearPublishedGraphOutputs();
    if (_shadowSettings) {
        _frameShadowSettings = *_shadowSettings;
    }
    loadPersistentSettings();
    YA_CORE_ASSERT(_render, "DeferredRenderPipeline requires a valid render backend");
    _defaultSkyboxMesh = PrimitiveMeshCache::get().getMesh(EPrimitiveGeometry::Cube);
    YA_CORE_ASSERT(_defaultSkyboxMesh != nullptr, "DeferredRenderPipeline requires default skybox cube mesh");
    resolveRuntimeFormats();

    Extent2D extent{
        .width  = static_cast<uint32_t>(desc.windowW),
        .height = static_cast<uint32_t>(desc.windowH),
    };

    initRenderTargetSpecs(extent);
    _entityIdPass.init(_render, EFormat::R32_UINT, _sharedDepthFormat);
    _currentGBufferResources.reset(buildGBufferSnapshotFormats());
    _currentViewportResources.reset(buildViewportSnapshotFormats());
    if (currentShadowSettings().isEnabled()) {
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
    if (_shadowResources.depthImage) {
        _shadowStage = ya::makeShared<ShadowStage>();
        _shadowStage->init(_render);
        if (_shadowResources.depthImage) {
            _shadowStage->refreshShadowResources(_shadowResources.depthImage, _shadowResources.depthFormat, _shadowResources.extent);
        }
    }

    _frameResources = ya::makeShared<DeferredFrameResourceSet>();
    _frameResources->init(_render);

    _gBufferStage = ya::makeShared<GBufferStage>();
    _gBufferStage->init(
        _render,
        _frameResources->getFrameAndLightDSL(),
        _frameResources->getSkinningDSL());

    _ssaoStage = ya::makeShared<SSAOStage>();
    _ssaoStage->setup(_currentGBufferResources);
    _ssaoStage->setSettings(_ssaoStage->getRadius(), _ssaoStage->getBias(), _ssaoStage->getPower(), _ssaoStage->getIntensity(), _bReverseViewportY);
    _ssaoStage->init(_render, _frameResources->getSSAOFrameDSL());

    _lightStage = ya::makeShared<LightStage>();
    _lightStage->setup(LightStage::SharedInputs{
        .frameAndLightDSL = _frameResources->getFrameAndLightDSL(),
    });
    _lightStage->setEnvironmentLightingInput(LightStage::EnvironmentLightingInput{
        .environmentLightingDSL = _environmentLightingDSL,
    });
    _lightStage->init(_render);
    syncShadowSettings();

    _overlayStage = ya::makeShared<ViewportOverlayStage>();
    _overlayStage->setDebugRenderSystem(_runtimeServices ? &_runtimeServices->getDebugRenderSystem() : nullptr);
    _overlayStage->init(_render, _frameResources->getSkyboxFrameDSL());

    refreshGBufferStageState();
    refreshViewportStageState();
}

void DeferredRenderPipeline::shutdown()
{
    _entityIdPass.destroy();
    _postProcessStage.shutdown();

    _debugAlbedoRGBView.reset();
    _debugSpecularAlphaView.reset();
    _cachedAlbedoSpecImageViewHandle = nullptr;
    _pendingViewportExtent           = {};
    _pendingResourceRefreshMask      = 0;
    clearPublishedGraphOutputs();
    _currentGBufferResources.reset();
    _currentViewportResources.reset();
    _currentEnvironmentLightingTextures = {};
    if (_ssaoStage) {
    }
    _graphExecutor.reset();

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
    if (_frameResources) {
        _frameResources->destroy();
        _frameResources.reset();
    }

    destroyShadowResources();
    _defaultSkyboxMesh = nullptr;
    _pendingSettings.reset();
    _environmentLightingDSL.reset();
    _runtimeServices = nullptr;
    _render                       = nullptr;
}


// ═══════════════════════════════════════════════════════════════════════
// Tick
// ═══════════════════════════════════════════════════════════════════════

void DeferredRenderPipeline::tick(const RenderPipelineFrameContext& frame)
{
    YA_PROFILE_FUNCTION();

    frame.cmdBuf->debugBeginLabel("Deferred Pipeline");

    if (shouldSkipTick(frame)) {
        return;
    }

    YA_PERF_SCOPE(perf::sample::deferredTick(), perf::metric::cpuTimeMs(), perf::domain::render());

    RenderStageContext stageCtx{};
    uint32_t           vpW = 0;
    uint32_t           vpH = 0;
    {
        YA_PROFILE_SCOPE("DeferredPipeline/BeginTick");
        beginTick(frame, stageCtx, vpW, vpH);
    }
    {
        YA_PROFILE_SCOPE("DeferredPipeline/SyncFrameSettings");
        syncFrameSettings(frame);
    }
    {
        YA_PROFILE_SCOPE("DeferredPipeline/ShadowPass");
        prepareShadowPass(stageCtx);
    }
    {
        YA_PROFILE_SCOPE("DeferredPipeline/MainGraph");
        executeDeferredMainGraph(frame, stageCtx, vpW, vpH);
    }

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
    applyPendingSettings();
    applyPendingResourceRefreshes();
    _postProcessStage.beginFrame();
    captureShadowSettings(frame);

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

void DeferredRenderPipeline::captureShadowSettings(const RenderPipelineFrameContext& frame)
{
    if (frame.shadowSettings) {
        _frameShadowSettings = *frame.shadowSettings;
    }
    else if (_shadowSettings) {
        _frameShadowSettings = *_shadowSettings;
    }
}

void DeferredRenderPipeline::updateStageFrameInputs(const RenderPipelineFrameContext& frame)
{
    Scene* activeScene = _runtimeServices ? _runtimeServices->getActiveScene() : nullptr;
    _currentEnvironmentLightingTextures =
        _runtimeServices
        ? _runtimeServices->resolveSceneEnvironmentLightingResources(activeScene)
        : EnvironmentLightingSceneResources{};

    if (_lightStage) {
        _currentEnvironmentLightingDescriptorSet = _runtimeServices
            ? _runtimeServices->getSceneEnvironmentLightingDescriptorSet(activeScene)
            : DescriptorSetHandle{};
        _lightStage->setFrameInputs(LightStage::FrameInputs{
            .frameAndLightDescriptorSet = _frameResources
                ? _frameResources->getBinding(frame.flightIndex).frameAndLightDescriptorSet
                : DescriptorSetHandle{},
            .environmentLightingDescriptorSet = _currentEnvironmentLightingDescriptorSet,
        });
    }
    // GBufferStage no longer receives frame inputs here: its current-flight
    // binding travels with DeferredGBufferPassParams in the graph pass (FG-302).

    _currentOverlayFrameInputs = {};
    if (_overlayStage) {
        ViewportOverlayStage::FrameInputs frameInputs{};
        frameInputs.skybox.frameDescriptorSet = _frameResources
            ? _frameResources->getBinding(frame.flightIndex).skyboxFrameDescriptorSet
            : DescriptorSetHandle{};
        auto* envProcessor = _runtimeServices ? _runtimeServices->getEnvironmentLightingProcessor() : nullptr;

        if (activeScene) {
            const float viewportHeight = static_cast<float>(frame.viewportRect.extent.y);
            if (viewportHeight > 0.0f) {
                for (const auto& [entity, billboard, transform] : activeScene->getRegistry().view<BillboardComponent, TransformComponent>().each()) {
                    (void)entity;
                    if (!shouldRenderBillboard(billboard)) {
                        continue;
                    }

                    const glm::vec3 worldCenter = transform.getWorldPosition();
                    const float distance        = glm::length(frame.cameraPos - worldCenter);
                    if (distance <= std::numeric_limits<float>::epsilon()) {
                        continue;
                    }

                    const float screenSizePixels = std::max(billboard.screenSizePixels, 1.0f);
                    const float scaleFactor      = screenSizePixels / viewportHeight;
                    const float size             = std::max(billboard.minWorldScale, scaleFactor * distance * 2.0f);

                    ViewportOverlayStage::FrameInputs::BillboardInput input{};
                    input.worldCenter    = worldCenter;
                    input.worldDirection = billboard.worldDirection;
                    input.worldSize      = glm::vec2(size, size);
                    input.tint           = billboard.tint;
                    input.entityId       = static_cast<uint32_t>(entity);
                    if (billboard.image.isReady()) {
                        input.textureBinding = ya::slotToTextureBinding(billboard.image);
                    }
                    frameInputs.billboards.push_back(std::move(input));
                }
            }

            const auto& dirView = activeScene->getRegistry().view<TransformComponent, DirectionComponent>();
            for (auto entity : dirView) {
                const auto& [tc, direction] = dirView.get(entity);
                (void)direction;
                frameInputs.directionGizmos.push_back(buildDirectionGizmoInput(tc));
            }
        }

        if (activeScene && envProcessor && _runtimeServices) {
            const auto* skyboxState = envProcessor->findFirstSceneSkyboxState(activeScene);
            if (skyboxState && skyboxState->hasRenderableCubemap()) {
                frameInputs.skybox.descriptorSet = _runtimeServices->getSceneSkyboxDescriptorSet(activeScene);
                frameInputs.skybox.mesh          = _defaultSkyboxMesh;
                for (const auto& [entity, sc, mc] : activeScene->getRegistry().view<SkyboxComponent, StaticMeshComponent>().each()) {
                    if (mc.isResolved() && mc.getMesh()) {
                        frameInputs.skybox.mesh = mc.getMesh();
                    }
                    break;
                }
                frameInputs.skybox.bAvailable = frameInputs.skybox.descriptorSet && frameInputs.skybox.mesh;
            }
        }

        _currentOverlayFrameInputs = frameInputs;
    }
}

void DeferredRenderPipeline::invalidateGBufferDependentViews()
{
    _cachedAlbedoSpecImageViewHandle = nullptr;
    _debugAlbedoRGBView.reset();
    _debugSpecularAlphaView.reset();

}

void DeferredRenderPipeline::clearPublishedGraphOutputs()
{
    _publishedGraphOutputs.clear();
}

void DeferredRenderPipeline::publishGraphExecutionResult(
    const RenderGraphExecutionResult& result,
    const DeferredFrameGraphResources& graphResources)
{
    auto nextGBuffer = buildPublishedGBufferResources(result);
    auto nextViewport = buildPublishedViewportResources(result, nextGBuffer.depthOwner);
    publishAttachmentResources(std::move(nextGBuffer), std::move(nextViewport));
    publishPostprocessOutputs(result, graphResources);
}

DeferredGBufferResources DeferredRenderPipeline::buildPublishedGBufferResources(
    const RenderGraphExecutionResult& result) const
{
    std::array<std::shared_ptr<RenderImage>, 4> nextGBufferColors{};
    for (uint32_t attachmentIndex = 0; attachmentIndex < std::size(deferred_graph_exports::gBufferColor); ++attachmentIndex) {
        nextGBufferColors[attachmentIndex] = result.getExportedTextureShared(deferred_graph_exports::gBufferColor[attachmentIndex]);
    }

    DeferredGBufferResources resources{};
    resources.publish(
        std::move(nextGBufferColors),
        result.getExportedTextureShared(deferred_graph_exports::gBufferDepth),
        buildGBufferSnapshotFormats());
    return resources;
}

DeferredViewportResources DeferredRenderPipeline::buildPublishedViewportResources(
    const RenderGraphExecutionResult& result,
    const std::shared_ptr<RenderImage>& depthOwner) const
{
    DeferredViewportResources resources{};
    resources.publish(
        result.getExportedTextureShared(deferred_graph_exports::viewportColor),
        depthOwner,
        result.getExportedTextureShared(deferred_graph_exports::entityId),
        buildViewportSnapshotFormats());
    return resources;
}

void DeferredRenderPipeline::publishAttachmentResources(
    DeferredGBufferResources nextGBuffer,
    DeferredViewportResources nextViewport)
{
    const bool bGBufferChanged = _currentGBufferResources.colorOwners != nextGBuffer.colorOwners ||
                                 _currentGBufferResources.depthOwner != nextGBuffer.depthOwner ||
                                 _currentGBufferResources.formats.colorFormats != nextGBuffer.formats.colorFormats ||
                                 _currentGBufferResources.formats.depthFormat != nextGBuffer.formats.depthFormat;
    const bool bViewportChanged = _currentViewportResources.colorOwner != nextViewport.colorOwner ||
                                  _currentViewportResources.depthOwner != nextViewport.depthOwner ||
                                  _currentViewportResources.formats.colorFormats != nextViewport.formats.colorFormats ||
                                  _currentViewportResources.formats.depthFormat != nextViewport.formats.depthFormat;

    _currentGBufferResources  = std::move(nextGBuffer);
    _currentViewportResources = std::move(nextViewport);

    if (bGBufferChanged) {
        refreshGBufferStageState();
    }
    if (bViewportChanged) {
        refreshViewportStageState();
    }
}

void DeferredRenderPipeline::publishPostprocessOutputs(
    const RenderGraphExecutionResult& result,
    const DeferredFrameGraphResources& graphResources)
{
    _publishedGraphOutputs.ssao = graphResources.textures.ssao.has_value()
        ? result.getExportedTextureShared(deferred_graph_exports::ssao)
        : nullptr;
    _publishedGraphOutputs.bloomExtract   = result.getExportedTextureShared(BloomPostprocessing::kExtractExportName);
    _publishedGraphOutputs.bloomBlur      = result.getExportedTextureShared(BloomPostprocessing::kBlurPongExportName);
    if (!_publishedGraphOutputs.bloomBlur) {
        _publishedGraphOutputs.bloomBlur = result.getExportedTextureShared(BloomPostprocessing::kBlurPingExportName);
    }
    _publishedGraphOutputs.bloomComposite = result.getExportedTextureShared(BloomPostprocessing::kOutputExportName);
    _publishedGraphOutputs.postprocess = graphResources.textures.postprocessOutput.has_value()
        ? result.getExportedTextureShared(PostProcessingStage::kOutputExportName)
        : nullptr;
}

DeferredAttachmentFormats DeferredRenderPipeline::buildGBufferSnapshotFormats() const
{
    return buildDeferredFormatsFromSpec(_gBufferRTSpec);
}

DeferredAttachmentFormats DeferredRenderPipeline::buildViewportSnapshotFormats() const
{
    DeferredAttachmentFormats formats = buildDeferredFormatsFromSpec(_viewportRTSpec);
    formats.depthFormat               = buildGBufferSnapshotFormats().depthFormat;
    return formats;
}

EFormat::T DeferredRenderPipeline::getViewportColorFormat() const
{
    if (_viewportRTSpec.attachments.colorAttach.empty()) {
        return EFormat::Undefined;
    }
    return _viewportRTSpec.attachments.colorAttach.front().format;
}

EFormat::T DeferredRenderPipeline::getViewportDepthFormat() const
{
    return buildGBufferSnapshotFormats().depthFormat.value_or(EFormat::Undefined);
}

void DeferredRenderPipeline::refreshGBufferStageState()
{
    invalidateGBufferDependentViews();

    if (_ssaoStage) {
        _ssaoStage->refreshPipelineFormat();
    }

    if (_gBufferStage) {
        _gBufferStage->refreshPipelineFormats(_currentGBufferResources.formats);
    }

    if (_lightStage) {
        _lightStage->setup(LightStage::SharedInputs{
            .frameAndLightDSL = _frameResources ? _frameResources->getFrameAndLightDSL() : nullptr,
        });
    }
}

void DeferredRenderPipeline::refreshViewportStageState()
{
    if (_lightStage) {
        _lightStage->refreshPipelineFormats(_currentViewportResources.formats);
    }

    if (_overlayStage) {
        _overlayStage->refreshPipelineFormats(_currentViewportResources.formats);
    }
}

void DeferredRenderPipeline::syncFrameSettings(const RenderPipelineFrameContext& frame)
{
    const float frameBufferScale = std::max(frame.viewportFrameBufferScale, 1.0f);
    const Extent2D desiredExtent  = Extent2D::fromVec2(frame.viewportRect.extent / frameBufferScale);
    if (desiredExtent.width > 0 && desiredExtent.height > 0 && desiredExtent != _viewportRTSpec.extent) {
        requestViewportResize(desiredExtent);
    }

    if (_ssaoStage) {
        _ssaoStage->setSettings(_ssaoStage->getRadius(),
                                _ssaoStage->getBias(),
                                _ssaoStage->getPower(),
                                _ssaoStage->getIntensity(),
                                _bReverseViewportY);
    }

    const auto     shadowSettings           = currentShadowSettings();
    const uint32_t shadowedPointLightBudget = shadowSettings.getEffectivePointLightCount();
    const uint32_t desiredShadowResolution  = std::max(shadowSettings.resolution, 1u);
    if (shadowSettings.isEnabled()) {
        const bool bShadowResolutionDirty = !_shadowResources.depthImage ||
                                            _shadowResources.extent.width != desiredShadowResolution ||
                                            _shadowResources.extent.height != desiredShadowResolution;
        if (bShadowResolutionDirty) {
            requestShadowResourceRefresh();
        }
    }

    (void)shadowedPointLightBudget;
    (void)shadowSettings;
    (void)desiredShadowResolution;
    syncShadowSettings();
    updateStageFrameInputs(frame);
}

void DeferredRenderPipeline::prepareShadowPass(RenderStageContext& stageCtx)
{
    const auto shadowSettings = currentShadowSettings();
    if (_shadowStage && shadowSettings.isEnabled()) {
        _shadowStage->applySettings(shadowSettings);
        {
            YA_PERF_SCOPE(perf::sample::deferredShadow(), perf::metric::cpuTimeMs(), perf::domain::render());
            _shadowStage->prepare(stageCtx);
        }
        return;
    }

    PerfState::Get().clearMetric(perf::sample::deferredShadow(), perf::metric::cpuTimeMs());
}

void DeferredRenderPipeline::executeDeferredMainGraph(const RenderPipelineFrameContext& frame, RenderStageContext& stageCtx, uint32_t vpW, uint32_t vpH)
{
    clearPublishedGraphOutputs();
    YA_CORE_ASSERT(_frameResources != nullptr, "Deferred pipeline frame resources are not initialized");
    if (!_frameResources->prepare(stageCtx)) {
        return;
    }
    const bool bUseSSAO = _bEnableSSAO && _ssaoStage;
    if (bUseSSAO && !_frameResources->prepareSSAO(stageCtx, _ssaoStage->buildFrameData(stageCtx))) {
        return;
    }
    if (_overlayStage && !_frameResources->prepareSkybox(stageCtx, _overlayStage->buildSkyboxFrameData(stageCtx))) {
        return;
    }

    const auto& frameBinding = _frameResources->getBinding(frame.flightIndex);
    // GBufferStage binding travels with DeferredGBufferPassParams in the graph
    // pass (FG-302); only stages that still read frame inputs are pre-set here.
    if (bUseSSAO) {
        _ssaoStage->setFrameInputs(SSAOStage::FrameInputs{
            .descriptorSet = frameBinding.ssaoFrameDescriptorSet,
            .frame         = frameBinding.ssaoFrame,
        });
    }
    _gBufferStage->prepare(stageCtx);

    _lastTickCtx = {
        .view       = frame.view,
        .projection = frame.projection,
        .cameraPos  = frame.cameraPos,
        .extent     = {.width = vpW, .height = vpH},
    };
    _lastFrameInput = frame;
    RenderGraph graph;
    DeferredFrameGraphResources graphResources{};
    _frameGraphOrchestrator.build(
        DeferredFrameGraphOrchestrator::BuildDependencies{
            .shadowStage      = _shadowStage.get(),
            .gBufferStage     = _gBufferStage.get(),
            .lightStage       = _lightStage.get(),
            .overlayStage     = _overlayStage.get(),
            .postProcessStage = &_postProcessStage,
            .ssaoStage        = _ssaoStage.get(),
            .entityIdPass     = &_entityIdPass,
        },
        DeferredFrameGraphOrchestrator::BuildInputs{
            .graph                    = &graph,
            .graphResources           = &graphResources,
            .stageCtx                 = &stageCtx,
            .frameBinding             = &frameBinding,
            .frame                    = &frame,
            .gBufferRTSpec            = &_gBufferRTSpec,
            .viewportRTSpec           = &_viewportRTSpec,
            .overlayInputs            = &_currentOverlayFrameInputs,
            .environmentLighting      = &_currentEnvironmentLightingTextures,
            .environmentLightingDS    = _currentEnvironmentLightingDescriptorSet,
            .postContext              = &_lastTickCtx,
            .viewportExtent           = _viewportRTSpec.extent,
            .bUseSSAO                 = bUseSSAO,
            .bReverseViewportY        = _bReverseViewportY,
            .bPostprocessOutputIsSRGB = EFormat::isSRGB(_render->getSwapchain()->getFormat()),
            .viewportOverlaySnapshot  = _lastFrameInput.viewportOverlaySnapshot,
        });

    YA_CORE_ASSERT(_graphExecutor != nullptr, "DeferredRenderPipeline graph executor is not initialized");
    RGCompiledGraph compiled{};
    RenderGraphExecutionResult result;
    if (!_graphExecutor->prepare(graph, compiled, &result)) {
        _lastFrameGraphTopology = {};
        clearPublishedGraphOutputs();
        return;
    }
    _lastFrameGraphTopology = graph.describeCompiledTopology(compiled);

    publishGraphExecutionResult(result, graphResources);

    if (_bEnableSSAO && _ssaoStage) {
        _ssaoStage->prepare(stageCtx);
    }

    if (_lightStage) {
        _lightStage->prepare(stageCtx);
    }
    if (_overlayStage) {
        _overlayStage->prepare(stageCtx);
        _overlayStage->updateBillboardTextures(_currentOverlayFrameInputs);
    }

    [[maybe_unused]] const bool bExecuted = _graphExecutor->executeCompiled(graph, compiled, *frame.cmdBuf);
    if (!bExecuted) {
        _lastFrameGraphTopology = {};
        clearPublishedGraphOutputs();
        return;
    }
}

// ═══════════════════════════════════════════════════════════════════════
// Depth Copy
// ═══════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════
// Viewport Pass
// ═══════════════════════════════════════════════════════════════════════

void DeferredRenderPipeline::onViewportResized(Rect2D rect)
{
    Extent2D newExtent{
        .width  = static_cast<uint32_t>(rect.extent.x),
        .height = static_cast<uint32_t>(rect.extent.y),
    };

    requestViewportResize(newExtent);
}

} // namespace ya
