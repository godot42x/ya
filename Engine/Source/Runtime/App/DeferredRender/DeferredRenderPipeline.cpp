#include "DeferredRenderPipeline.h"

#include "Config/ConfigManager.h"
#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Core/Profiling/Profiling.h"
#include "DeferredViewportResources.h"
#include "DeferredAttachmentFormats.h"
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
#include "Runtime/App/App.h"
#include "Runtime/App/Common/Shadow/Common/ShadowSettingsConfig.h"
#include "Render/Core/RenderGraphExecutor.h"
#include <algorithm>
#include <chrono>
#include <format>

namespace ya
{

namespace
{

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

constexpr const char* DEFERRED_PIPELINE_CONFIG_DOC_NAME                       = "editor";
constexpr const char* DEFERRED_PIPELINE_CONFIG_KEY_ENABLE_SSAO                = "render.deferred.ssao.enabled";

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

stdptr<Texture> makeCompatTextureFromRenderImage(RenderImage* image, std::string_view label)
{
    if (!image || !image->getImageShared() || !image->getImageViewShared()) {
        return nullptr;
    }

    return Texture::wrap(image->getImageShared(), image->getImageViewShared(), std::string(label));
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

    if (_gBufferStage) {
        _gBufferStage->applyShadowState(shadowState);
    }
}

ShadowSettings DeferredRenderPipeline::currentShadowSettings() const
{
    return _frameShadowSettings;
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

void DeferredRenderPipeline::queueShadowSettingsChange(const ShadowSettings& shadowSettings)
{
    _pendingShadowSettings = shadowSettings;
    shadow_settings::saveEditorSettings(_pendingShadowSettings);

    if (_bShadowSettingsChangePending || !_queueFrameTask) {
        if (!_queueFrameTask) {
            applyShadowSettings(_pendingShadowSettings);
        }
        return;
    }

    _bShadowSettingsChangePending = true;
    _queueFrameTask([this]()
                    {
                        _bShadowSettingsChangePending = false;

                        applyShadowSettings(_pendingShadowSettings);
                    });
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
    shadow_settings::saveEditorSettings(shadowSettings);
}

void DeferredRenderPipeline::loadPersistentSettings()
{
    auto& cfgManager = ConfigManager::get();
    _bEnableSSAO                    = cfgManager.getOr<bool>(DEFERRED_PIPELINE_CONFIG_DOC_NAME,
                                          DEFERRED_PIPELINE_CONFIG_KEY_ENABLE_SSAO,
                                          _bEnableSSAO);
    const ShadowSettings baselineShadowSettings = _shadowSettings ? *_shadowSettings : currentShadowSettings();
    ShadowSettings shadowSettings = shadow_settings::loadEditorSettings(baselineShadowSettings, _automationShadowOverrides);
    if (_shadowSettings) {
        *_shadowSettings = shadowSettings;
    }
    _pendingShadowSettings = shadowSettings;
}

void DeferredRenderPipeline::saveShadowSettingsToConfig(const ShadowSettings& shadowSettings) const
{
    shadow_settings::saveEditorSettings(shadowSettings);
}

DeferredPipelineDebugViews DeferredRenderPipeline::buildDebugViews() const
{
    return DeferredPipelineDebugViews{
        .gBufferResources  = _currentGBufferResources,
        .viewportResources = _currentViewportResources,
        .ssaoTextureOwner  = _currentSSAOOutput,
    };
}

void DeferredRenderPipeline::appendRenderTargetEditorEntries(RenderTargetEditorCatalog& catalog) const
{
    const DeferredAttachmentFormats gbufferFormats  = buildGBufferSnapshotFormats();
    const DeferredAttachmentFormats viewportFormats = buildViewportSnapshotFormats();
    catalog.entries.push_back({
        .label        = "Deferred GBuffer",
        .owner        = RenderTargetEditorCatalog::Entry::EOwner::DeferredGBuffer,
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
        .owner        = RenderTargetEditorCatalog::Entry::EOwner::DeferredViewport,
        .colorFormats = viewportFormats.colorFormats,
        .depthFormat  = viewportFormats.depthFormat,
        .colorAttachments = {_currentViewportResources.colorOwner},
        .depthAttachment  = _currentViewportResources.depthOwner,
        .extent           = _viewportRTSpec.extent,
        .frameBufferCount = 1,
    });
    catalog.entries.push_back({
        .label               = "Deferred Shadow",
        .owner               = RenderTargetEditorCatalog::Entry::EOwner::DeferredShadow,
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
    RenderTargetEditorCatalog::Entry::EOwner owner,
    EFormat::T format)
{
    switch (owner) {
    case RenderTargetEditorCatalog::Entry::EOwner::DeferredGBuffer:
    case RenderTargetEditorCatalog::Entry::EOwner::DeferredViewport:
        setDeferredSharedDepthFormat(format);
        return true;
    case RenderTargetEditorCatalog::Entry::EOwner::DeferredShadow:
        if (_shadowDepthFormat != format) {
            _shadowDepthFormat = format;
            requestShadowResourceRefresh();
        }
        return true;
    default:
        return false;
    }
}

bool DeferredRenderPipeline::setRenderTargetColorFormat(RenderTargetEditorCatalog::Entry::EOwner owner,
                                                        uint32_t attachmentIndex,
                                                        EFormat::T format)
{
    bool bFormatChanged = false;
    switch (owner) {
    case RenderTargetEditorCatalog::Entry::EOwner::DeferredGBuffer:
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
    case RenderTargetEditorCatalog::Entry::EOwner::DeferredViewport:
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
        _render->waitIdle();

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

    _render->waitIdle();
}

void DeferredRenderPipeline::initPipelineState(const InitDesc& desc)
{
    _render                       = desc.render;
    _graphExecutor                = _render ? std::make_unique<RenderGraphExecutor>(*_render->getResourceFactory()) : nullptr;
    _shadowSettings               = desc.shadowSettings;
    _automationShadowOverrides    = desc.automationShadowOverrides;
    _queueFrameTask               = desc.queueFrameTask;
    _environmentLightingDSL       = desc.environmentLightingDSL;
    _getSceneEnvironmentLightingDescriptorSet = desc.getSceneEnvironmentLightingDescriptorSet;
    _resolveSceneEnvironmentLightingTextures  = desc.resolveSceneEnvironmentLightingTextures;
    _getSceneSkyboxDescriptorSet  = desc.getSceneSkyboxDescriptorSet;
    _getDebugRenderSystem         = desc.getDebugRenderSystem;
    _getActiveScene               = desc.getActiveScene;
    _getResourceResolveSystem     = desc.getResourceResolveSystem;
    _bShadowSettingsChangePending = false;
    _pendingResourceRefreshMask   = 0;
    _currentSSAOOutput.reset();
    _currentPostprocessOutput.reset();
    if (_shadowSettings) {
        _frameShadowSettings = *_shadowSettings;
    }
    loadPersistentSettings();
    YA_CORE_ASSERT(_render, "DeferredRenderPipeline requires a valid render backend");
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

    _gBufferStage = ya::makeShared<GBufferStage>();
    _gBufferStage->init(_render);

    _ssaoStage = ya::makeShared<SSAOStage>();
    _ssaoStage->setup(_currentGBufferResources);
    _ssaoStage->setSettings(_ssaoStage->getRadius(), _ssaoStage->getBias(), _ssaoStage->getPower(), _ssaoStage->getIntensity(), _bReverseViewportY);
    _ssaoStage->init(_render);

    _lightStage = ya::makeShared<LightStage>();
    _lightStage->setup(LightStage::SharedInputs{
        .frameAndLightDSL = _gBufferStage->getFrameAndLightDSL(),
    }, _currentGBufferResources);
    _lightStage->setEnvironmentLightingInput(LightStage::EnvironmentLightingInput{
        .environmentLightingDSL = _environmentLightingDSL,
        .getSceneEnvironmentLightingDescriptorSet = _getSceneEnvironmentLightingDescriptorSet,
    });
    _lightStage->setSSAOTexture(_currentSSAOOutput.get());
    _lightStage->init(_render);
    syncShadowSettings();

    _overlayStage = ya::makeShared<ViewportOverlayStage>();
    _overlayStage->setServices(ViewportOverlayStage::Services{
        .getDebugRenderSystem = _getDebugRenderSystem,
    });
    _overlayStage->init(_render);

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
    _viewportTextureCompat.reset();
    _viewportDepthTextureCompat.reset();
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

    destroyShadowResources();
    _bShadowSettingsChangePending = false;
    _environmentLightingDSL.reset();
    _getSceneEnvironmentLightingDescriptorSet = {};
    _resolveSceneEnvironmentLightingTextures = {};
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
    frame.cmdBuf->debugBeginLabel("Deferred Pipeline");

    if (shouldSkipTick(frame)) {
        return;
    }

    YA_PERF_SCOPE(perf::sample::deferredTick(), perf::metric::cpuTimeMs(), perf::domain::render());

    RenderStageContext stageCtx{};
    uint32_t           vpW = 0;
    uint32_t           vpH = 0;
    beginTick(frame, stageCtx, vpW, vpH);
    syncFrameSettings(frame);
    prepareShadowPass(stageCtx);
    executeDeferredMainGraph(frame, stageCtx, vpW, vpH);

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
        _resolveSceneEnvironmentLightingTextures
        ? _resolveSceneEnvironmentLightingTextures(activeScene)
        : RenderSharedResourceProvider::EnvironmentLightingTextureSet{};

    if (_lightStage) {
        _lightStage->setFrameInputs(LightStage::FrameInputs{
            .frameAndLightDescriptorSet = _gBufferStage ? _gBufferStage->getFrameAndLightDS(frame.flightIndex) : DescriptorSetHandle{},
            .environmentLightingDescriptorSet = _getSceneEnvironmentLightingDescriptorSet
                ? _getSceneEnvironmentLightingDescriptorSet(activeScene)
                : DescriptorSetHandle{},
        });
    }

    if (_overlayStage) {
        ViewportOverlayStage::FrameInputs frameInputs{};
        auto* resourceResolveSystem = _getResourceResolveSystem ? _getResourceResolveSystem() : nullptr;

        if (activeScene) {
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
                frameInputs.skybox.mesh          = PrimitiveMeshCache::get().getMesh(EPrimitiveGeometry::Cube);
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
    const RenderGraphResourceRegistry& registry,
    const std::array<RGTextureHandle, 4>& gbufferColors,
    RGTextureHandle gbufferDepth,
    RGTextureHandle viewportColor)
{
    DeferredGBufferResources nextGBuffer{};
    for (uint32_t attachmentIndex = 0; attachmentIndex < gbufferColors.size(); ++attachmentIndex) {
        nextGBuffer.colorOwners[attachmentIndex] = registry.resolveTextureShared(gbufferColors[attachmentIndex]);
    }
    nextGBuffer.depthOwner = registry.resolveTextureShared(gbufferDepth);
    nextGBuffer.formats    = buildGBufferSnapshotFormats();
    nextGBuffer.syncRawViews();

    DeferredViewportResources nextViewport{};
    nextViewport.colorOwner = registry.resolveTextureShared(viewportColor);
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
        refreshViewportCompatTextures();
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

void DeferredRenderPipeline::refreshViewportCompatTextures()
{
    _viewportTextureCompat      = makeCompatTextureFromRenderImage(_currentViewportResources.color, "DeferredViewportCompatColor");
    _viewportDepthTextureCompat = makeCompatTextureFromRenderImage(_currentViewportResources.depth, "DeferredViewportCompatDepth");
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
            .frameAndLightDSL = _gBufferStage ? _gBufferStage->getFrameAndLightDSL() : nullptr,
        }, _currentGBufferResources);
    }
}

void DeferredRenderPipeline::refreshViewportStageState()
{
    if (_lightStage) {
        _lightStage->setSSAOTexture(_currentSSAOOutput.get());
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
        _lightStage->setSSAOTexture(_bEnableSSAO ? _currentSSAOOutput.get() : nullptr);
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
    _gBufferStage->prepare(stageCtx);

    RenderGraph graph;
    std::optional<RGPassHandle> shadowPass;
    if (_shadowStage && currentShadowSettings().isEnabled()) {
        shadowPass = _shadowStage->appendGraphPasses(graph, stageCtx);
    }
    auto importHostWrittenBuffer = [&](const stdptr<IBuffer>& buffer, std::string label, EBufferUsage usage) {
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
                .offset = 0,
                .size   = buffer->getSize(),
            },
            .retainedResources = {buffer},
        });
    };
    const auto frameBuffer = importHostWrittenBuffer(
        _gBufferStage->getFrameBufferOwner(frame.flightIndex),
        "Deferred.FrameUBO",
        EBufferUsage::UniformBuffer);
    const auto lightBuffer = importHostWrittenBuffer(
        _gBufferStage->getLightBufferOwner(frame.flightIndex),
        "Deferred.LightUBO",
        EBufferUsage::UniformBuffer);
    const auto skinningBuffer = importHostWrittenBuffer(
        _gBufferStage->getSkinningBufferOwner(frame.flightIndex),
        "Deferred.SkinningSSBO",
        EBufferUsage::StorageBuffer);

    std::array<RGTextureHandle, 4> gbufferColors{};
    for (uint32_t attachmentIndex = 0; attachmentIndex < gbufferColors.size(); ++attachmentIndex) {
        gbufferColors[attachmentIndex] = graph.createTexture(
            makeGraphAttachmentDesc(
                _gBufferRTSpec,
                _gBufferRTSpec.attachments.colorAttach[attachmentIndex],
                std::format("DeferredGBuffer.Color{}", attachmentIndex)),
            ERGResourceLifetime::Persistent);
    }
    YA_CORE_ASSERT(_gBufferRTSpec.attachments.depthAttach.has_value(), "Deferred GBuffer graph requires a depth attachment spec");
    const auto gbufferDepthHandle = graph.createTexture(
        makeGraphAttachmentDesc(_gBufferRTSpec, *_gBufferRTSpec.attachments.depthAttach, "DeferredGBuffer.Depth"),
        ERGResourceLifetime::Persistent);
    const Extent2D gbufferExtent = _gBufferRTSpec.extent;

    [[maybe_unused]] const auto gbufferPass = graph.addPass(
        "Deferred GBuffer",
        [&](RGPassBuilder& passBuilder) {
            passBuilder.read(frameBuffer);
            passBuilder.read(lightBuffer);
            passBuilder.read(skinningBuffer);
            for (const auto handle : gbufferColors) {
                passBuilder.useColorAttachment(handle);
            }
            passBuilder.useDepthAttachment(gbufferDepthHandle);
        },
        [&](RGRenderContext& rgCtx) {
            rgCtx.beginRasterRendering({
                .renderArea = Rect2D{.pos = {0, 0}, .extent = gbufferExtent.toVec2()},
                .layerCount = 1,
                .colors = {
                    {.color = gbufferColors[0], .clearValue = ClearValue(0.0f, 0.0f, 0.0f, 1.0f), .loadOp = EAttachmentLoadOp::Clear, .storeOp = EAttachmentStoreOp::Store, .finalLayout = EImageLayout::ShaderReadOnlyOptimal},
                    {.color = gbufferColors[1], .clearValue = ClearValue(0.0f, 0.0f, 0.0f, 1.0f), .loadOp = EAttachmentLoadOp::Clear, .storeOp = EAttachmentStoreOp::Store, .finalLayout = EImageLayout::ShaderReadOnlyOptimal},
                    {.color = gbufferColors[2], .clearValue = ClearValue(0.0f, 0.0f, 0.0f, 0.0f), .loadOp = EAttachmentLoadOp::Clear, .storeOp = EAttachmentStoreOp::Store, .finalLayout = EImageLayout::ShaderReadOnlyOptimal},
                    {.color = gbufferColors[3], .clearValue = ClearValue(0.0f, 0.0f, 0.0f, 0.0f), .loadOp = EAttachmentLoadOp::Clear, .storeOp = EAttachmentStoreOp::Store, .finalLayout = EImageLayout::ShaderReadOnlyOptimal},
                },
                .depth = RGRenderContext::DepthRenderingDesc{
                    .depth       = gbufferDepthHandle,
                    .clearValue  = ClearValue(1.0f, 0),
                    .loadOp      = EAttachmentLoadOp::Clear,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                },
            });

            float gbVpY = 0.0f;
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

    _lastTickCtx = {
        .view       = frame.view,
        .projection = frame.projection,
        .cameraPos  = frame.cameraPos,
        .extent     = {.width = vpW, .height = vpH},
    };
    _lastFrameInput = frame;

    YA_CORE_ASSERT(!_viewportRTSpec.attachments.colorAttach.empty(), "Deferred viewport graph requires a color attachment spec");
    const auto color = graph.createTexture(
        makeGraphAttachmentDesc(_viewportRTSpec, _viewportRTSpec.attachments.colorAttach.front(), "DeferredViewport.Color"),
        ERGResourceLifetime::Persistent);
    const auto viewportDepthHandle = gbufferDepthHandle;
    const Extent2D viewportExtent = _viewportRTSpec.extent;

    std::optional<RGTextureHandle> ssao;
    if (_bEnableSSAO && _ssaoStage) {
        ssao = _ssaoStage->appendGraphPass(graph, stageCtx, gbufferColors[0], gbufferColors[1], gbufferDepthHandle);
    }

    std::optional<RGTextureHandle> environmentCubemap;
    std::optional<RGTextureHandle> environmentIrradiance;
    std::optional<RGTextureHandle> environmentPrefilter;
    std::optional<RGTextureHandle> environmentBrdfLut;
    if (_currentEnvironmentLightingTextures.isComplete()) {
        environmentCubemap = graph.importTexture(
            makeImportedTextureDesc(*_currentEnvironmentLightingTextures.cubemapTexture,
                                    "DeferredLight.Environment.Cubemap",
                                    EImageLayout::ShaderReadOnlyOptimal));
        environmentIrradiance = graph.importTexture(
            makeImportedTextureDesc(*_currentEnvironmentLightingTextures.irradianceTexture,
                                    "DeferredLight.Environment.Irradiance",
                                    EImageLayout::ShaderReadOnlyOptimal));
        environmentPrefilter = graph.importTexture(
            makeImportedTextureDesc(*_currentEnvironmentLightingTextures.prefilterTexture,
                                    "DeferredLight.Environment.Prefilter",
                                    EImageLayout::ShaderReadOnlyOptimal));
        environmentBrdfLut = graph.importTexture(
            makeImportedTextureDesc(*_currentEnvironmentLightingTextures.brdfLutTexture,
                                    "DeferredLight.Environment.BrdfLut",
                                    EImageLayout::ShaderReadOnlyOptimal));
    }

    std::optional<RGTextureHandle> shadowDepth;
    if (auto shadowDepthImage = getShadowDepthImage();
        shadowDepthImage && _shadowResources.directionalDepthIV && isShadowMappingEnabled()) {
        shadowDepth = graph.importTexture(
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
            if (shadowPass.has_value()) {
                passBuilder.dependsOn(*shadowPass);
            }
            passBuilder.read(frameBuffer);
            passBuilder.read(lightBuffer);
            for (const auto handle : gbufferColors) {
                passBuilder.read(handle);
            }
            if (ssao.has_value()) {
                passBuilder.read(*ssao);
            }
            if (shadowDepth.has_value()) {
                passBuilder.read(*shadowDepth);
            }
            if (environmentCubemap.has_value()) {
                passBuilder.read(*environmentCubemap);
            }
            if (environmentIrradiance.has_value()) {
                passBuilder.read(*environmentIrradiance);
            }
            if (environmentPrefilter.has_value()) {
                passBuilder.read(*environmentPrefilter);
            }
            if (environmentBrdfLut.has_value()) {
                passBuilder.read(*environmentBrdfLut);
            }
            passBuilder.useColorAttachment(color);
        },
        [&](RGRenderContext& rgCtx) {
            rgCtx.beginColorRendering({
                .color       = color,
                .renderArea  = {.pos = {0, 0}, .extent = viewportExtent.toVec2()},
                .clearValue  = ClearValue(0.0f, 0.0f, 0.0f, 0.0f),
                .loadOp      = EAttachmentLoadOp::Clear,
                .storeOp     = EAttachmentStoreOp::Store,
                .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
            });

            YA_PERF_SCOPE(perf::sample::deferredLight(), perf::metric::cpuTimeMs(), perf::domain::render());
            _lightStage->execute(stageCtx);
            rgCtx.endRendering();
        });

    [[maybe_unused]] const auto skyboxPass = graph.addPass(
            "Deferred Skybox",
        [&](RGPassBuilder& passBuilder) {
            passBuilder.useColorAttachment(color);
            passBuilder.useDepthAttachment(viewportDepthHandle);
        },
        [&](RGRenderContext& rgCtx) {
            rgCtx.beginRasterRendering({
                .renderArea = {.pos = {0, 0}, .extent = viewportExtent.toVec2()},
                .layerCount = 1,
                .colors = {{
                    .color       = color,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                }},
                .depth = RGRenderContext::DepthRenderingDesc{
                    .depth       = viewportDepthHandle,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                },
            });

            {
                _overlayStage->executeSkybox(stageCtx);
            }

            rgCtx.endRendering();
        });

    [[maybe_unused]] const auto overlayPass = graph.addPass(
        "Deferred Scene Overlay",
        [&](RGPassBuilder& passBuilder) {
            passBuilder.useColorAttachment(color);
            passBuilder.useDepthAttachment(viewportDepthHandle);
        },
        [&](RGRenderContext& rgCtx) {
            rgCtx.beginRasterRendering({
                .renderArea = {.pos = {0, 0}, .extent = viewportExtent.toVec2()},
                .layerCount = 1,
                .colors = {{
                    .color       = color,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                }},
                .depth = RGRenderContext::DepthRenderingDesc{
                    .depth       = viewportDepthHandle,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                },
            });

            {
                YA_PERF_SCOPE(perf::sample::deferredOverlay(), perf::metric::cpuTimeMs(), perf::domain::render());
                _overlayStage->executeOverlay(stageCtx);
            }

            rgCtx.endRendering();
        });

    [[maybe_unused]] const auto viewportOverlayPass = graph.addPass(
        "Deferred Viewport Overlay",
        [&](RGPassBuilder& passBuilder) {
            passBuilder.useColorAttachment(color);
            passBuilder.useDepthAttachment(viewportDepthHandle);
        },
        [&](RGRenderContext& rgCtx) {
            rgCtx.beginRasterRendering({
                .renderArea = {.pos = {0, 0}, .extent = viewportExtent.toVec2()},
                .layerCount = 1,
                .colors = {{
                    .color       = color,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                }},
                .depth = RGRenderContext::DepthRenderingDesc{
                    .depth       = viewportDepthHandle,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                },
            });

            if (_lastFrameInput.recordViewportOverlays) {
                YA_PERF_SCOPE(perf::sample::renderViewportOverlay(), perf::metric::cpuTimeMs(), perf::domain::render());
                _lastFrameInput.recordViewportOverlays(&rgCtx.getCommandBuffer(), viewportExtent, _lastTickCtx);
            }

            rgCtx.endRendering();
        });

    const auto postprocessOutput = _postProcessStage.appendGraphPasses(
        graph,
        color,
        viewportExtent,
        &_lastTickCtx);

    YA_CORE_ASSERT(_graphExecutor != nullptr, "DeferredRenderPipeline graph executor is not initialized");
    RGCompiledGraph compiled{};
    if (!_graphExecutor->prepare(graph, compiled)) {
        _currentSSAOOutput.reset();
        _currentPostprocessOutput.reset();
        if (_lightStage) {
            _lightStage->setSSAOTexture(nullptr);
        }
        _postProcessStage.clearPreparedResources();
        return;
    }

    syncGraphAttachmentSnapshots(
        _graphExecutor->getRegistry(),
        gbufferColors,
        gbufferDepthHandle,
        color);

    if (_bEnableSSAO && _ssaoStage) {
        _ssaoStage->prepare(stageCtx);
    }

    if (ssao.has_value()) {
        _currentSSAOOutput = _graphExecutor->getRegistry().resolveTextureShared(*ssao);
        if (_lightStage) {
            _lightStage->setSSAOTexture(_currentSSAOOutput.get());
        }
    }
    else {
        _currentSSAOOutput.reset();
        if (_lightStage) {
            _lightStage->setSSAOTexture(nullptr);
        }
    }

    if (_lightStage) {
        _lightStage->prepare(stageCtx);
    }
    if (_overlayStage) {
        _overlayStage->prepare(stageCtx);
    }
    _postProcessStage.resolvePreparedResources(_graphExecutor->getRegistry());

    [[maybe_unused]] const bool bExecuted = _graphExecutor->executeCompiled(graph, compiled, *frame.cmdBuf);
    if (!bExecuted) {
        _currentSSAOOutput.reset();
        _currentPostprocessOutput.reset();
        if (_lightStage) {
            _lightStage->setSSAOTexture(nullptr);
        }
        _postProcessStage.clearPreparedResources();
        return;
    }

    if (postprocessOutput.isValid()) {
        if (_postProcessStage.getPreparedOutputImage()) {
            _currentPostprocessOutput = _postProcessStage.getPreparedOutputImageShared();
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
    if (!_shadowSettings) {
        return;
    }
    const ShadowSettings currentShadowSettings = *_shadowSettings;
    ShadowSettings       pendingShadowSettings = currentShadowSettings;
    bool                 bShadowSettingsDirty  = false;

    bool bShadowEnabled = pendingShadowSettings.isEnabled();
    if (ImGui::Checkbox("Enable Shadow Mapping", &bShadowEnabled)) {
        if (bShadowEnabled) {
            if (pendingShadowSettings.quality == EShadowQuality::Off) {
                pendingShadowSettings.applyQualityPreset(EShadowQuality::Medium);
            }
        }
        else {
            pendingShadowSettings.quality = EShadowQuality::Off;
        }
        bShadowSettingsDirty = true;
    }

    if (pendingShadowSettings.isEnabled()) {
        static const char* qualityNames[] = {"Low", "Medium", "High", "Ultra"};
        int                qualityIdx     = std::max(0, static_cast<int>(pendingShadowSettings.quality) - 1);
        if (ImGui::Combo("Quality Preset", &qualityIdx, qualityNames, IM_ARRAYSIZE(qualityNames))) {
            auto newQuality = static_cast<EShadowQuality::T>(qualityIdx + 1);
            pendingShadowSettings.applyQualityPreset(newQuality);
            bShadowSettingsDirty = true;
        }

        if (ImGui::Checkbox("Directional Shadow", &pendingShadowSettings.directionalEnabled)) {
            bShadowSettingsDirty = true;
        }
        if (ImGui::Checkbox("Point Light Shadow", &pendingShadowSettings.pointLightEnabled)) {
            bShadowSettingsDirty = true;
        }
        if (ImGui::Checkbox("Point Light Indirect Draw", &pendingShadowSettings.pointLightUseIndirect)) {
            bShadowSettingsDirty = true;
        }
        if (ImGui::Checkbox("Point Light Indirect Cull", &pendingShadowSettings.pointLightIndirectCullEnabled)) {
            bShadowSettingsDirty = true;
        }
        int maxPL = static_cast<int>(pendingShadowSettings.maxPointLightShadows);
        if (ImGui::SliderInt("Max Point Shadows", &maxPL, 0, MAX_POINT_LIGHTS)) {
            pendingShadowSettings.maxPointLightShadows = static_cast<uint32_t>(maxPL);
            bShadowSettingsDirty = true;
        }

        int shadowResolution = static_cast<int>(pendingShadowSettings.resolution);
        if (ImGui::DragInt("Shadow Resolution", &shadowResolution, 16.0f, 128, 8192, "%d")) {
            pendingShadowSettings.resolution = static_cast<uint32_t>(std::clamp(shadowResolution, 128, 8192));
            bShadowSettingsDirty = true;
        }

        if (ImGui::DragFloat("Depth Bias", &pendingShadowSettings.bias, 0.0001f, 0.0f, 0.1f, "%.5f")) {
            bShadowSettingsDirty = true;
        }
        if (ImGui::DragFloat("Normal Bias", &pendingShadowSettings.normalBias, 0.0001f, 0.0f, 0.1f, "%.5f")) {
            bShadowSettingsDirty = true;
        }
        if (ImGui::DragFloat("Directional Distance", &pendingShadowSettings.directionalDistance, 0.5f, 1.0f, 500.0f, "%.1f")) {
            bShadowSettingsDirty = true;
        }
        if (ImGui::Checkbox("Stable Directional Fit", &pendingShadowSettings.directionalStableFit)) {
            bShadowSettingsDirty = true;
        }
        int directionalCascades = static_cast<int>(pendingShadowSettings.directionalCascades);
        if (ImGui::SliderInt("Directional Cascades", &directionalCascades, 0, 4)) {
            pendingShadowSettings.directionalCascades = static_cast<uint32_t>(directionalCascades);
            bShadowSettingsDirty = true;
        }

        static const char* filterNames[] = {"Hard", "PCF Low", "PCF High"};
        int                currentFilter = static_cast<int>(pendingShadowSettings.filter);
        if (ImGui::Combo("Shadow Filter", &currentFilter, filterNames, IM_ARRAYSIZE(filterNames))) {
            pendingShadowSettings.filter = static_cast<EShadowFilter::T>(currentFilter);
            bShadowSettingsDirty = true;
        }
    }

    if (bShadowSettingsDirty) {
        queueShadowSettingsChange(pendingShadowSettings);
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
    const float renderCallbacksMs  = cpu(perf::sample::frameRenderCallbacks());
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
                drawPerfLeaf("RenderCallbacks", renderCallbacksMs, runtimeMs);
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
