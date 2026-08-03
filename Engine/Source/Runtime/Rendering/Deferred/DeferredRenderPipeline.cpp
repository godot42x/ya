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
#include "ECS/Component/TransformComponent.h"
#include "ECS/System/ResourceResolveSystem.h"
#include "Render/Core/Sampler.h"
#include "Render/Core/RenderGraphImportUtils.h"
#include "Render/Core/RenderImage.h"
#include "Render/Core/Texture.h"
#include "Resource/Mesh/PrimitiveMeshCache.h"
#include "Runtime/Application/App.h"
#include "Runtime/Rendering/Common/Shadow/Common/ShadowSettingsConfig.h"
#include "Render/Core/RenderGraphExecutor.h"
#include "Config/ConfigManager.h"

#include "Scene/Scene.h"
#include <algorithm>
#include <chrono>
#include <format>

namespace ya
{

namespace
{

constexpr std::string_view kDeferredGBufferColorExportNames[4] = {
    "Deferred.GBuffer.Color0",
    "Deferred.GBuffer.Color1",
    "Deferred.GBuffer.Color2",
    "Deferred.GBuffer.Color3",
};
constexpr std::string_view kDeferredGBufferDepthExportName  = "Deferred.GBuffer.Depth";
constexpr std::string_view kDeferredViewportColorExportName = "Deferred.Viewport.Color";
constexpr std::string_view kDeferredSSAOExportName          = "Deferred.SSAO.Output";

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
        .ssaoTextureOwner  = _currentSSAOOutput,
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
    _getSceneEnvironmentLightingDescriptorSet = desc.getSceneEnvironmentLightingDescriptorSet;
    _resolveSceneEnvironmentLightingResources = desc.resolveSceneEnvironmentLightingResources;
    _getSceneSkyboxDescriptorSet  = desc.getSceneSkyboxDescriptorSet;
    _getDebugRenderSystem         = desc.getDebugRenderSystem;
    _getActiveScene               = desc.getActiveScene;
    _getResourceResolveSystem     = desc.getResourceResolveSystem;
    _pendingSettings.reset();
    _pendingResourceRefreshMask   = 0;
    _currentSSAOOutput.reset();
    _currentPostprocessOutput.reset();
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
    _currentGBufferResources.formats  = buildGBufferSnapshotFormats();
    _currentViewportResources.formats = buildViewportSnapshotFormats();
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
    }, _currentGBufferResources);
    _lightStage->setEnvironmentLightingInput(LightStage::EnvironmentLightingInput{
        .environmentLightingDSL = _environmentLightingDSL,
        .getSceneEnvironmentLightingDescriptorSet = _getSceneEnvironmentLightingDescriptorSet,
    });
    _lightStage->setSSAOTexture(_currentSSAOOutput);
    _lightStage->init(_render);
    syncShadowSettings();

    _overlayStage = ya::makeShared<ViewportOverlayStage>();
    _overlayStage->setServices(ViewportOverlayStage::Services{
        .getDebugRenderSystem = _getDebugRenderSystem,
    });
    _overlayStage->init(_render, _frameResources->getSkyboxFrameDSL());

    refreshGBufferStageState();
    refreshViewportStageState();
}

void DeferredRenderPipeline::shutdown()
{
    _postProcessStage.shutdown();

    _debugAlbedoRGBView.reset();
    _debugSpecularAlphaView.reset();
    _cachedAlbedoSpecImageViewHandle = nullptr;
    _pendingViewportExtent           = {};
    _pendingResourceRefreshMask      = 0;
    _currentSSAOOutput.reset();
    _currentPostprocessOutput.reset();
    _currentGBufferResources         = {};
    _currentViewportResources        = {};
    _currentEnvironmentLightingTextures = {};
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
    _getSceneEnvironmentLightingDescriptorSet = {};
    _resolveSceneEnvironmentLightingResources = {};
    _getSceneSkyboxDescriptorSet = {};
    _getDebugRenderSystem = {};
    _getActiveScene = {};
    _getResourceResolveSystem = {};
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
    Scene* activeScene = _getActiveScene ? _getActiveScene() : nullptr;
    _currentEnvironmentLightingTextures =
        _resolveSceneEnvironmentLightingResources
        ? _resolveSceneEnvironmentLightingResources(activeScene)
        : EnvironmentLightingSceneResources{};

    if (_lightStage) {
        _lightStage->setFrameInputs(LightStage::FrameInputs{
            .frameAndLightDescriptorSet = _frameResources
                ? _frameResources->getBinding(frame.flightIndex).frameAndLightDescriptorSet
                : DescriptorSetHandle{},
            .environmentLightingDescriptorSet = _getSceneEnvironmentLightingDescriptorSet
                ? _getSceneEnvironmentLightingDescriptorSet(activeScene)
                : DescriptorSetHandle{},
        });
    }
    if (_gBufferStage) {
        _gBufferStage->setFrameInputs(GBufferStage::FrameInputs{
            .frameAndLightDescriptorSet = _frameResources
                ? _frameResources->getBinding(frame.flightIndex).frameAndLightDescriptorSet
                : DescriptorSetHandle{},
            .skinningDescriptorSet = _frameResources
                ? _frameResources->getBinding(frame.flightIndex).skinningDescriptorSet
                : DescriptorSetHandle{},
        });
    }

    if (_overlayStage) {
        ViewportOverlayStage::FrameInputs frameInputs{};
        frameInputs.skybox.frameDescriptorSet = _frameResources
            ? _frameResources->getBinding(frame.flightIndex).skyboxFrameDescriptorSet
            : DescriptorSetHandle{};
        auto* resourceResolveSystem = _getResourceResolveSystem ? _getResourceResolveSystem() : nullptr;

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
                    if (billboard.image.isReady()) {
                        input.textureBinding = billboard.image.toTextureBinding();
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

        if (activeScene && resourceResolveSystem && _getSceneSkyboxDescriptorSet) {
            const auto* skyboxState = resourceResolveSystem->findFirstSceneSkyboxState(activeScene);
            if (skyboxState && skyboxState->hasRenderableCubemap()) {
                frameInputs.skybox.descriptorSet = _getSceneSkyboxDescriptorSet(activeScene);
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

        _overlayStage->setFrameInputs(std::move(frameInputs));
    }
}

void DeferredRenderPipeline::invalidateGBufferDependentViews()
{
    _cachedAlbedoSpecImageViewHandle = nullptr;
    _debugAlbedoRGBView.reset();
    _debugSpecularAlphaView.reset();

    if (_ssaoStage) {
        _ssaoStage->invalidateInputDescriptors();
    }
    if (_lightStage) {
        _lightStage->invalidateGBufferDescriptors();
    }
}

void DeferredRenderPipeline::syncGraphAttachmentSnapshots(
    const RenderGraphExecutionResult& result)
{
    DeferredGBufferResources nextGBuffer{};
    for (uint32_t attachmentIndex = 0; attachmentIndex < std::size(kDeferredGBufferColorExportNames); ++attachmentIndex) {
        nextGBuffer.colorOwners[attachmentIndex] = result.getExportedTextureShared(kDeferredGBufferColorExportNames[attachmentIndex]);
    }
    nextGBuffer.depthOwner = result.getExportedTextureShared(kDeferredGBufferDepthExportName);
    nextGBuffer.formats    = buildGBufferSnapshotFormats();
    nextGBuffer.syncRawViews();

    DeferredViewportResources nextViewport{};
    nextViewport.colorOwner = result.getExportedTextureShared(kDeferredViewportColorExportName);
    nextViewport.depthOwner = nextGBuffer.depthOwner;
    nextViewport.formats    = buildViewportSnapshotFormats();
    nextViewport.syncRawViews();

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
        _ssaoStage->setup(_currentGBufferResources);
        _ssaoStage->refreshPipelineFormat();
    }

    if (_gBufferStage) {
        _gBufferStage->refreshPipelineFormats(_currentGBufferResources.formats);
    }

    if (_lightStage) {
        _lightStage->setup(LightStage::SharedInputs{
            .frameAndLightDSL = _frameResources ? _frameResources->getFrameAndLightDSL() : nullptr,
        }, _currentGBufferResources);
    }
}

void DeferredRenderPipeline::refreshViewportStageState()
{
    if (_lightStage) {
        _lightStage->setSSAOTexture(_currentSSAOOutput);
        _lightStage->refreshPipelineFormats(_currentViewportResources.formats);
    }

    if (_overlayStage) {
        _overlayStage->refreshPipelineFormats(_currentViewportResources.formats);
    }
}

void DeferredRenderPipeline::syncFrameSettings(const RenderPipelineFrameContext& frame)
{
    (void)frame;

    if (_lightStage) {
        _lightStage->setSSAOTexture(_bEnableSSAO ? _currentSSAOOutput : nullptr);
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
    _currentSSAOOutput.reset();
    _currentPostprocessOutput.reset();
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
    _gBufferStage->setFrameInputs(GBufferStage::FrameInputs{
        .frameAndLightDescriptorSet = frameBinding.frameAndLightDescriptorSet,
        .skinningDescriptorSet      = frameBinding.skinningDescriptorSet,
    });
    if (bUseSSAO) {
        _ssaoStage->setFrameInputs(SSAOStage::FrameInputs{
            .descriptorSet = frameBinding.ssaoFrameDescriptorSet,
            .frame         = frameBinding.ssaoFrame,
        });
    }
    if (_overlayStage) {
        _overlayStage->setSkyboxFrameDescriptorSet(frameBinding.skyboxFrameDescriptorSet);
    }
    _gBufferStage->prepare(stageCtx);

    RenderGraph graph;
    DeferredFrameGraphResources graphResources{};
    if (_shadowStage && currentShadowSettings().isEnabled()) {
        graphResources.passes.shadow = _shadowStage->appendGraphPasses(graph, stageCtx);
    }
    auto importHostWrittenBuffer = [&](const stdptr<IBuffer>& buffer,
                                       std::string label,
                                       EBufferUsage usage,
                                       uint64_t rangeOffset = 0,
                                       uint64_t rangeSize = 0) {
        YA_CORE_ASSERT(buffer != nullptr, "Deferred graph requires imported buffer '{}'", label);
        return graph.importBuffer(RGImportedBufferDesc{
            .desc = RGBufferDesc{
                .label = std::move(label),
                .usage = usage,
                .size  = buffer->getSize(),
            },
            .buffer = buffer.get(),
            .initialState = BufferResourceState{
                .stages = EPipelineStage::Host,
                .access = EResourceAccess::HostWrite,
                .offset = rangeOffset,
                .size   = rangeSize == 0 ? buffer->getSize() : rangeSize,
            },
            .retainedResources = {buffer},
        });
    };
    graphResources.buffers.frame = importHostWrittenBuffer(
        frameBinding.frame.buffer,
        "Deferred.FrameUBO",
        EBufferUsage::UniformBuffer,
        frameBinding.frame.offset,
        frameBinding.frame.size);
    graphResources.buffers.light = importHostWrittenBuffer(
        frameBinding.light.buffer,
        "Deferred.LightUBO",
        EBufferUsage::UniformBuffer,
        frameBinding.light.offset,
        frameBinding.light.size);
    graphResources.buffers.skinning = importHostWrittenBuffer(
        frameBinding.skinningBuffer,
        "Deferred.SkinningSSBO",
        EBufferUsage::StorageBuffer);
    if (bUseSSAO) {
        graphResources.buffers.ssaoFrame = importHostWrittenBuffer(
            frameBinding.ssaoFrame.buffer,
            "Deferred.SSAOFrameUBO",
            EBufferUsage::UniformBuffer,
            frameBinding.ssaoFrame.offset,
            frameBinding.ssaoFrame.size);
    }
    graphResources.buffers.skyboxFrame = importHostWrittenBuffer(
        frameBinding.skyboxFrame.buffer,
        "Deferred.SkyboxFrameUBO",
        EBufferUsage::UniformBuffer,
        frameBinding.skyboxFrame.offset,
        frameBinding.skyboxFrame.size);

    for (uint32_t attachmentIndex = 0; attachmentIndex < graphResources.textures.gBufferColors.size(); ++attachmentIndex) {
        graphResources.textures.gBufferColors[attachmentIndex] = graph.createPersistentTexture(
            makeGraphAttachmentDesc(
                _gBufferRTSpec,
                _gBufferRTSpec.attachments.colorAttach[attachmentIndex],
                std::format("DeferredGBuffer.Color{}", attachmentIndex)),
            RGPersistentTextureKey{.value = std::format("DeferredGBuffer.Color{}", attachmentIndex)});
    }
    YA_CORE_ASSERT(_gBufferRTSpec.attachments.depthAttach.has_value(), "Deferred GBuffer graph requires a depth attachment spec");
    graphResources.textures.gBufferDepth = graph.createPersistentTexture(
        makeGraphAttachmentDesc(_gBufferRTSpec, *_gBufferRTSpec.attachments.depthAttach, "DeferredGBuffer.Depth"),
        RGPersistentTextureKey{.value = "DeferredGBuffer.Depth"});
    const Extent2D gbufferExtent = _gBufferRTSpec.extent;

    [[maybe_unused]] const auto gbufferPass = graph.addPass(
        "Deferred GBuffer",
        [&](RGPassBuilder& passBuilder) {
            passBuilder.uniformRead(graphResources.buffers.frame, RGBufferRange{
                .offset = frameBinding.frame.offset,
                .size   = frameBinding.frame.size,
            });
            passBuilder.uniformRead(graphResources.buffers.light, RGBufferRange{
                .offset = frameBinding.light.offset,
                .size   = frameBinding.light.size,
            });
            passBuilder.storageRead(graphResources.buffers.skinning);
            passBuilder.declareRaster({
                .renderArea = Rect2D{.pos = {0, 0}, .extent = gbufferExtent.toVec2()},
                .layerCount = 1,
                .colors = {
                    {.color = graphResources.textures.gBufferColors[0], .clearValue = ClearValue(0.0f, 0.0f, 0.0f, 1.0f), .loadOp = EAttachmentLoadOp::Clear, .storeOp = EAttachmentStoreOp::Store, .finalLayout = EImageLayout::ShaderReadOnlyOptimal},
                    {.color = graphResources.textures.gBufferColors[1], .clearValue = ClearValue(0.0f, 0.0f, 0.0f, 1.0f), .loadOp = EAttachmentLoadOp::Clear, .storeOp = EAttachmentStoreOp::Store, .finalLayout = EImageLayout::ShaderReadOnlyOptimal},
                    {.color = graphResources.textures.gBufferColors[2], .clearValue = ClearValue(0.0f, 0.0f, 0.0f, 0.0f), .loadOp = EAttachmentLoadOp::Clear, .storeOp = EAttachmentStoreOp::Store, .finalLayout = EImageLayout::ShaderReadOnlyOptimal},
                    {.color = graphResources.textures.gBufferColors[3], .clearValue = ClearValue(0.0f, 0.0f, 0.0f, 0.0f), .loadOp = EAttachmentLoadOp::Clear, .storeOp = EAttachmentStoreOp::Store, .finalLayout = EImageLayout::ShaderReadOnlyOptimal},
                },
                .depth = RGDepthAttachmentDesc{
                    .depth       = graphResources.textures.gBufferDepth,
                    .clearValue  = ClearValue(1.0f, 0),
                    .loadOp      = EAttachmentLoadOp::Clear,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                },
            });
        },
        [&](RGRenderContext& rgCtx) {
            const auto rasterParams = rgCtx.getRasterPassExecutionParams();
            rgCtx.beginDeclaredRasterRendering();

            float gbVpY = 0.0f;
            const auto gbufferExtent = rasterParams.getRenderExtent();
            const auto vpW           = gbufferExtent.width;
            const auto vpH           = gbufferExtent.height;
            float gbVpH = static_cast<float>(vpH);
            if (_bReverseViewportY) {
                gbVpY = static_cast<float>(vpH);
                gbVpH = -gbVpH;
            }
            rgCtx.getCommandBuffer().setViewport(0.0f, gbVpY, static_cast<float>(vpW), gbVpH);
            rgCtx.getCommandBuffer().setScissor(0, 0, vpW, vpH);

            _gBufferStage->execute(stageCtx);
            rgCtx.endRendering();
        });
    graphResources.passes.gBuffer = gbufferPass;

    _lastTickCtx = {
        .view       = frame.view,
        .projection = frame.projection,
        .cameraPos  = frame.cameraPos,
        .extent     = {.width = vpW, .height = vpH},
    };
    _lastFrameInput = frame;

    YA_CORE_ASSERT(!_viewportRTSpec.attachments.colorAttach.empty(), "Deferred viewport graph requires a color attachment spec");
    graphResources.textures.viewportColor = graph.createPersistentTexture(
        makeGraphAttachmentDesc(_viewportRTSpec, _viewportRTSpec.attachments.colorAttach.front(), "DeferredViewport.Color"),
        RGPersistentTextureKey{.value = "DeferredViewport.Color"});
    const Extent2D viewportExtent = _viewportRTSpec.extent;

    if (bUseSSAO) {
        YA_CORE_ASSERT(graphResources.buffers.ssaoFrame.has_value(), "Deferred SSAO requires an imported frame buffer");
        graphResources.textures.ssao = _ssaoStage->appendGraphPass(
            graph,
            stageCtx,
            *graphResources.buffers.ssaoFrame,
            RGBufferRange{.offset = frameBinding.ssaoFrame.offset, .size = frameBinding.ssaoFrame.size},
            graphResources.textures.gBufferColors[0],
            graphResources.textures.gBufferColors[1],
            graphResources.textures.gBufferDepth);
    }

    if (_currentEnvironmentLightingTextures.isComplete()) {
        graphResources.textures.environmentCubemap = graph.importTexture(
            makeDeferredEnvironmentImportedDesc(_currentEnvironmentLightingTextures.cubemap,
                                                "DeferredLight.Environment.Cubemap"));
        graphResources.textures.environmentIrradiance = graph.importTexture(
            makeDeferredEnvironmentImportedDesc(_currentEnvironmentLightingTextures.irradiance,
                                                "DeferredLight.Environment.Irradiance"));
        graphResources.textures.environmentPrefilter = graph.importTexture(
            makeDeferredEnvironmentImportedDesc(_currentEnvironmentLightingTextures.prefilter,
                                                "DeferredLight.Environment.Prefilter"));
        graphResources.textures.environmentBrdfLut = graph.importTexture(
            makeImportedTextureDesc(*_currentEnvironmentLightingTextures.brdfLut,
                                    "DeferredLight.Environment.BrdfLut",
                                    EImageLayout::ShaderReadOnlyOptimal));
    }

    if (auto shadowDepthImage = getShadowDepthImage();
        shadowDepthImage && _shadowResources.directionalDepthIV && isShadowMappingEnabled()) {
        graphResources.textures.shadowDepth = graph.importTexture(
            makeImportedSubresourceTextureDesc(
                shadowDepthImage,
                ImageViewCreateInfo{
                    .label          = "DeferredLight.ShadowDepth.FullArrayView",
                    .viewType       = EImageViewType::View2DArray,
                    .aspectFlags    = EImageAspect::Depth,
                    .baseMipLevel   = 0,
                    .levelCount     = 1,
                    .baseArrayLayer = 0,
                    .layerCount     = _shadowResources.layerCount,
                },
                Extent3D{_shadowResources.extent.width, _shadowResources.extent.height, 1},
                "DeferredLight.ShadowDepth",
                EImageLayout::ShaderReadOnlyOptimal,
                EImageUsage::Sampled));
    }

    [[maybe_unused]] const auto lightPass = graph.addPass(
        "Deferred Light",
        [&](RGPassBuilder& passBuilder) {
            if (graphResources.passes.shadow.has_value()) {
                passBuilder.dependsOn(*graphResources.passes.shadow);
            }
            passBuilder.uniformRead(graphResources.buffers.frame, RGBufferRange{
                .offset = frameBinding.frame.offset,
                .size   = frameBinding.frame.size,
            });
            passBuilder.uniformRead(graphResources.buffers.light, RGBufferRange{
                .offset = frameBinding.light.offset,
                .size   = frameBinding.light.size,
            });
            for (const auto handle : graphResources.textures.gBufferColors) {
                passBuilder.read(handle);
            }
            if (graphResources.textures.ssao.has_value()) {
                passBuilder.read(*graphResources.textures.ssao);
            }
            if (graphResources.textures.shadowDepth.has_value()) {
                passBuilder.read(*graphResources.textures.shadowDepth);
            }
            if (graphResources.textures.environmentCubemap.has_value()) {
                passBuilder.read(*graphResources.textures.environmentCubemap);
            }
            if (graphResources.textures.environmentIrradiance.has_value()) {
                passBuilder.read(*graphResources.textures.environmentIrradiance);
            }
            if (graphResources.textures.environmentPrefilter.has_value()) {
                passBuilder.read(*graphResources.textures.environmentPrefilter);
            }
            if (graphResources.textures.environmentBrdfLut.has_value()) {
                passBuilder.read(*graphResources.textures.environmentBrdfLut);
            }
            passBuilder.declareRaster({
                .renderArea = {.pos = {0, 0}, .extent = viewportExtent.toVec2()},
                .layerCount = 1,
                .colors = {{
                    .color       = graphResources.textures.viewportColor,
                    .clearValue  = ClearValue(0.0f, 0.0f, 0.0f, 0.0f),
                    .loadOp      = EAttachmentLoadOp::Clear,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                }},
            });
        },
        [&](RGRenderContext& rgCtx) {
            [[maybe_unused]] const auto rasterParams = rgCtx.getRasterPassExecutionParams();
            rgCtx.beginDeclaredRasterRendering();

            YA_PERF_SCOPE(perf::sample::deferredLight(), perf::metric::cpuTimeMs(), perf::domain::render());
            _lightStage->execute(stageCtx);
            rgCtx.endRendering();
        });
    graphResources.passes.light = lightPass;

    [[maybe_unused]] const auto skyboxPass = graph.addPass(
            "Deferred Skybox",
        [&](RGPassBuilder& passBuilder) {
            passBuilder.uniformRead(graphResources.buffers.skyboxFrame, RGBufferRange{
                .offset = frameBinding.skyboxFrame.offset,
                .size   = frameBinding.skyboxFrame.size,
            });
            passBuilder.declareRaster({
                .renderArea = {.pos = {0, 0}, .extent = viewportExtent.toVec2()},
                .layerCount = 1,
                .colors = {{
                    .color       = graphResources.textures.viewportColor,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = graphResources.textures.gBufferDepth,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                },
            });
        },
        [&](RGRenderContext& rgCtx) {
            [[maybe_unused]] const auto rasterParams = rgCtx.getRasterPassExecutionParams();
            rgCtx.beginDeclaredRasterRendering();

            {
                _overlayStage->executeSkybox(stageCtx);
            }

            rgCtx.endRendering();
        });
    graphResources.passes.skybox = skyboxPass;

    const auto bloomComposite = _postProcessStage.appendBloomGraphPasses(
        graph,
        graphResources.textures.viewportColor,
        viewportExtent,
        &_lastTickCtx);
    if (bloomComposite.isValid()) {
        graphResources.textures.bloomComposite = bloomComposite;
    }
    graphResources.textures.overlayInput = bloomComposite.isValid() ? bloomComposite : graphResources.textures.viewportColor;

    [[maybe_unused]] const auto overlayPass = graph.addPass(
        "Deferred Scene Overlay",
        [&](RGPassBuilder& passBuilder) {
            passBuilder.declareRaster({
                .renderArea = {.pos = {0, 0}, .extent = viewportExtent.toVec2()},
                .layerCount = 1,
                .colors = {{
                    .color       = graphResources.textures.overlayInput,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = graphResources.textures.gBufferDepth,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                },
            });
        },
        [&](RGRenderContext& rgCtx) {
            [[maybe_unused]] const auto rasterParams = rgCtx.getRasterPassExecutionParams();
            rgCtx.beginDeclaredRasterRendering();

            {
                YA_PERF_SCOPE(perf::sample::deferredOverlay(), perf::metric::cpuTimeMs(), perf::domain::render());
                _overlayStage->executeOverlay(stageCtx);
            }

            rgCtx.endRendering();
        });
    graphResources.passes.sceneOverlay = overlayPass;

    [[maybe_unused]] const auto viewportOverlayPass = graph.addPass(
        "Deferred Viewport Overlay",
        [&](RGPassBuilder& passBuilder) {
            passBuilder.declareRaster({
                .renderArea = {.pos = {0, 0}, .extent = viewportExtent.toVec2()},
                .layerCount = 1,
                .colors = {{
                    .color       = graphResources.textures.overlayInput,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = graphResources.textures.gBufferDepth,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                },
            });
        },
        [&](RGRenderContext& rgCtx) {
            const auto rasterParams   = rgCtx.getRasterPassExecutionParams();
            const auto viewportExtent = rasterParams.getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();

            if (_lastFrameInput.recordViewportOverlays) {
                YA_PERF_SCOPE(perf::sample::renderViewportOverlay(), perf::metric::cpuTimeMs(), perf::domain::render());
                _lastFrameInput.recordViewportOverlays(&rgCtx.getCommandBuffer(), viewportExtent, _lastTickCtx);
            }

            rgCtx.endRendering();
        });
    graphResources.passes.viewportOverlay = viewportOverlayPass;

    const auto postprocessOutput = _postProcessStage.appendFinalizeGraphPasses(
        graph,
        graphResources.textures.overlayInput,
        viewportExtent,
        &_lastTickCtx);
    if (postprocessOutput.isValid()) {
        graphResources.textures.postprocessOutput = postprocessOutput;
    }

    for (uint32_t attachmentIndex = 0; attachmentIndex < graphResources.textures.gBufferColors.size(); ++attachmentIndex) {
        graph.exportTexture(graphResources.textures.gBufferColors[attachmentIndex], std::string(kDeferredGBufferColorExportNames[attachmentIndex]));
    }
    graph.exportTexture(graphResources.textures.gBufferDepth, std::string(kDeferredGBufferDepthExportName));
    graph.exportTexture(graphResources.textures.viewportColor, std::string(kDeferredViewportColorExportName));
    if (graphResources.textures.ssao.has_value()) {
        graph.exportTexture(*graphResources.textures.ssao, std::string(kDeferredSSAOExportName));
    }

    YA_CORE_ASSERT(_graphExecutor != nullptr, "DeferredRenderPipeline graph executor is not initialized");
    RGCompiledGraph compiled{};
    RenderGraphExecutionResult result;
    if (!_graphExecutor->prepare(graph, compiled, &result)) {
        _currentSSAOOutput.reset();
        _currentPostprocessOutput.reset();
        if (_lightStage) {
            _lightStage->setSSAOTexture({});
        }
        _postProcessStage.clearPreparedResources();
        return;
    }

    syncGraphAttachmentSnapshots(result);

    if (_bEnableSSAO && _ssaoStage) {
        _ssaoStage->prepare(stageCtx);
    }

    if (graphResources.textures.ssao.has_value()) {
        _currentSSAOOutput = result.getExportedTextureShared(kDeferredSSAOExportName);
        if (_lightStage) {
            _lightStage->setSSAOTexture(_currentSSAOOutput);
        }
    }
    else {
        _currentSSAOOutput.reset();
        if (_lightStage) {
            _lightStage->setSSAOTexture({});
        }
    }

    if (_lightStage) {
        _lightStage->prepare(stageCtx);
    }
    if (_overlayStage) {
        _overlayStage->prepare(stageCtx);
    }
    _postProcessStage.capturePreparedResources(result);

    [[maybe_unused]] const bool bExecuted = _graphExecutor->executeCompiled(graph, compiled, *frame.cmdBuf);
    if (!bExecuted) {
        _currentSSAOOutput.reset();
        _currentPostprocessOutput.reset();
        if (_lightStage) {
            _lightStage->setSSAOTexture({});
        }
        _postProcessStage.clearPreparedResources();
        return;
    }

    if (graphResources.textures.postprocessOutput.has_value()) {
        if (auto preparedOutput = _postProcessStage.getPreparedOutputImageShared()) {
            _currentPostprocessOutput = std::move(preparedOutput);
            return;
        }
    }

    _currentPostprocessOutput.reset();
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
