#include "Render3D/Deferred/DeferredRenderPipeline.h"

#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Core/Profiling/Profiling.h"
#include "Render3D/Deferred/DeferredAttachmentFormats.h"
#include "ECS/Component/2D/BillboardComponent.h"
#include "ECS/Component/3D/SkyboxComponent.h"
#include "ECS/Systems/Components/DirectionComponent.h"
#include "ECS/Component/Mesh/StaticMeshComponent.h"
#include "Scene3D/TransformComponent.h"
#include "Render/Resources/TextureSlotBinding.h"
#include "Render3D/Common/PipelineCommon.h"
#include "Render3D/Common/RenderRecordingContext.h"
#include "Render3D/Common/RenderSubmission.h"
#include "Render3D/Common/ViewPassResources.h"
#include "Render3D/Common/ViewTargetStore.h"
#include "Render3D/Common/ViewGraphName.h"
#include "Render3D/Common/SceneRenderScheduler.h"
#include "Render3D/Pipelines/BloomPostprocessing.h"
#include "Render3D/EnvironmentLighting/EnvironmentLightingProcessor.h"
#include "RHI/Core/Sampler.h"
#include "Graph/RenderGraphImportUtils.h"
#include "RHI/Core/RenderTexture.h"
#include "RHI/Core/Swapchain.h"
#include "RHI/Core/Texture.h"
#include "Resource/Mesh/PrimitiveMeshCache.h"

#include "Render3D/Common/PostProcessingStateConfig.h"
#include "Render3D/Common/Shadow/Common/ShadowSettingsConfig.h"
#include "Graph/RenderGraphExecutor.h"
#include "Graph/RenderGraph.h"
#include "Core/Config/ConfigManager.h"
#include "Scene/Core/Scene.h"
#include <algorithm>
#include <chrono>
#include <format>
#include <limits>
#include <vector>

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

ViewOverlayStage::FrameInputs::DirectionGizmoInput buildDirectionGizmoInput(const TransformComponent& tc)
{
    const glm::mat4 worldTransform = glm::translate(glm::mat4(1.0f), tc.getWorldPosition()) *
                                     glm::mat4_cast(glm::quat(glm::radians(tc.getRotation())));
    const glm::mat4 coneLocalTransf =
        glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1, 0, 0)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(0.3f, 1.0f, 0.3f));
    const glm::mat4 cylinderLocalTransf =
        glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1, 0, 0)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(0.1f, 1.0f, 0.1f));

    return ViewOverlayStage::FrameInputs::DirectionGizmoInput{
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
DeferredAttachmentFormats buildDeferredViewFormats(EFormat::T colorFormat, EFormat::T depthFormat);
RenderTargetCreateInfo buildDeferredGBufferRenderTargetSpec(Extent2D extent,
                                                            EFormat::T signedLinearFormat,
                                                            EFormat::T linearFormat,
                                                            EFormat::T shadingModelFormat,
                                                            EFormat::T depthFormat);
RenderTargetCreateInfo buildDeferredViewRenderTargetSpec(Extent2D extent, EFormat::T colorFormat);
DeferredAttachmentFormats buildDeferredFormatsFromSpec(const RenderTargetCreateInfo& spec);

RGImportedTextureDesc makeDeferredEnvironmentImportedDesc(const std::shared_ptr<ImageResource>& resource,
                                                          std::string_view                     label)
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

DeferredAttachmentFormats buildDeferredViewFormats(EFormat::T colorFormat, EFormat::T depthFormat)
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

RenderTargetCreateInfo buildDeferredViewRenderTargetSpec(Extent2D extent, EFormat::T colorFormat)
{
    return RenderTargetCreateInfo{
        .label            = "Deferred View RT",
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

void allocateDeferredViewPassResources(
    RenderSubmission&                         submission,
    IRender*                                  render,
    uint32_t                                  alignment,
    const RenderFrameData*                    frameData,
    SSAOStage*                                ssaoStage,
    LightStage*                               lightStage,
    EntityIdPass*                     entityIdPass,
    ViewOverlayStage*                     overlayStage,
    PostProcessingStage*                      postStage,
    ViewOverlayStage::FrameInputs*        overlayInputs,
    DeferredFrameResourceSet::ViewResources&  resources)
{
    if (ssaoStage) {
        resources.ssao.inputs.set = allocateCombinedImageSamplerSet(submission, ssaoStage->getInputDSL(), 4);
    }
    if (lightStage) {
        resources.lighting.gBufferTextures.set = allocateCombinedImageSamplerSet(
            submission, lightStage->getGBufferTextureDSL(), 5);
        resources.lighting.shadows.set = allocateCombinedImageSamplerSet(
            submission,
            lightStage->getShadowDSL(),
            1u + static_cast<uint32_t>(MAX_POINT_LIGHTS));
        lightStage->writeShadowDescriptors(resources.lighting.shadows.set);
    }
    if (entityIdPass && frameData) {
        EntityIdPass::FrameUBO ubo{};
        ubo.viewProj = frameData->viewProjection;
        ubo.view     = frameData->view;
        writeUniformPassBinding(
            submission,
            render,
            entityIdPass->getFrameDSL(),
            alignment,
            &ubo,
            sizeof(ubo),
            resources.entityId.frame);
    }
    if (overlayStage && frameData) {
        ViewOverlayStage::BillboardFrameUBO ubo{
            .viewProjection = frameData->viewProjection,
            .view           = frameData->view,
        };
        writeUniformPassBinding(
            submission,
            render,
            overlayStage->getBillboardFrameDSL(),
            alignment,
            &ubo,
            sizeof(ubo),
            resources.overlay.billboardFrame);
        resources.overlay.billboardTextures.set = allocateCombinedImageSamplerSet(
            submission,
            overlayStage->getBillboardTextureDSL(),
            ViewOverlayStage::kBillboardTextureCount);
        if (overlayInputs) {
            overlayStage->updateBillboardTextures(*overlayInputs, resources.overlay);
        }
    }
    if (postStage) {
        allocateBloomPassBindings(
            submission,
            postStage->getBloomExtractDSL(),
            postStage->getBloomBlurDSL(),
            postStage->getBloomCompositeDSL(),
            resources.post.bloom);
        resources.post.toneMap.input.set = allocateCombinedImageSamplerSet(
            submission, postStage->getToneMapInputDSL(), 1);
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
    _viewColorFormat = chooseSupportedAttachmentFormat(
        _render,
        "Deferred View Color",
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
    _viewRTSpec = buildDeferredViewRenderTargetSpec(extent, _viewColorFormat);
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

RenderPipelineSettings DeferredRenderPipeline::buildSettingsSnapshot() const
{
    return {
        .kind              = ERenderPipelineKind::Deferred,
        .bReverseViewportY = _bReverseViewportY,
        .bSSAOEnabled      = _bEnableSSAO,
        .ssaoRadius        = _ssaoStage ? _ssaoStage->getRadius() : _ssaoRadius,
        .ssaoBias          = _ssaoStage ? _ssaoStage->getBias() : _ssaoBias,
        .ssaoPower         = _ssaoStage ? _ssaoStage->getPower() : _ssaoPower,
        .ssaoIntensity     = _ssaoStage ? _ssaoStage->getIntensity() : _ssaoIntensity,
        .bPBRDiffuseIBL    = _lightStage ? _lightStage->isPBRDiffuseIBLEnabled() : _bEnablePBRDiffuseIBL,
        .bPBRSpecularIBL   = _lightStage ? _lightStage->isPBRSpecularIBLEnabled() : _bEnablePBRSpecularIBL,
        .shadow            = currentShadowSettings(),
        .postProcessing    = _postProcessStage.getState(),
    };
}

RenderPipelineSettings DeferredRenderPipeline::resolveSettings() const
{
    return _pendingSettings ? *_pendingSettings : buildSettingsSnapshot();
}

void DeferredRenderPipeline::requestSettings(const RenderPipelineSettings& settings)
{
    _pendingSettings = settings;
}

bool DeferredRenderPipeline::isShadowMappingEnabled() const
{
    return currentShadowSettings().isEnabled();
}

std::shared_ptr<ImageResource> DeferredRenderPipeline::getShadowDirectionalDepthResource() const
{
    return makeShadowDebugResource(
        _shadowResources.depthImage,
        _shadowResources.directionalDepthIV,
        "Deferred.ShadowDirectionalDepth");
}

std::shared_ptr<ImageResource> DeferredRenderPipeline::getShadowPointFaceDepthResource(
    uint32_t pointLightIndex,
    uint32_t faceIndex) const
{
    if (pointLightIndex >= MAX_POINT_LIGHTS || faceIndex >= 6) {
        return nullptr;
    }

    return makeShadowDebugResource(
        _shadowResources.depthImage,
        _shadowResources.pointFaceIVs[pointLightIndex][faceIndex],
        std::format("Deferred.ShadowPointDepth.{}.{}", pointLightIndex, faceIndex));
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

    const RenderPipelineSettings settings = std::move(*_pendingSettings);
    _pendingSettings.reset();

    _bReverseViewportY = settings.bReverseViewportY;
    setSSAOEnabled(settings.bSSAOEnabled);
    _ssaoRadius            = settings.ssaoRadius;
    _ssaoBias              = settings.ssaoBias;
    _ssaoPower             = settings.ssaoPower;
    _ssaoIntensity         = settings.ssaoIntensity;
    _bEnablePBRDiffuseIBL  = settings.bPBRDiffuseIBL;
    _bEnablePBRSpecularIBL = settings.bPBRSpecularIBL;
    _postProcessStage.getState() = settings.postProcessing;
    applyShadowSettings(settings.shadow);
    if (_ssaoStage) {
        _ssaoStage->setSettings(_ssaoRadius, _ssaoBias, _ssaoPower, _ssaoIntensity, _bReverseViewportY);
    }
    if (_lightStage) {
        _lightStage->setIBLSettings(_bEnablePBRDiffuseIBL, _bEnablePBRSpecularIBL);
    }
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
    _ssaoRadius        = config.getOr<float>(RUNTIME_CONFIG_DOCUMENT, "render.deferred.ssao.radius", _ssaoRadius);
    _ssaoBias          = config.getOr<float>(RUNTIME_CONFIG_DOCUMENT, "render.deferred.ssao.bias", _ssaoBias);
    _ssaoPower         = config.getOr<float>(RUNTIME_CONFIG_DOCUMENT, "render.deferred.ssao.power", _ssaoPower);
    _ssaoIntensity     = config.getOr<float>(RUNTIME_CONFIG_DOCUMENT, "render.deferred.ssao.intensity", _ssaoIntensity);
    _bEnablePBRDiffuseIBL  = config.getOr<bool>(RUNTIME_CONFIG_DOCUMENT, "render.deferred.light.enablePBRDiffuseIBL", _bEnablePBRDiffuseIBL);
    _bEnablePBRSpecularIBL = config.getOr<bool>(RUNTIME_CONFIG_DOCUMENT, "render.deferred.light.enablePBRSpecularIBL", _bEnablePBRSpecularIBL);

    _postProcessStage.getState() = postprocess_settings::loadRuntimeSettings(_postProcessStage.getState());

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

void DeferredRenderPipeline::appendRenderTargetEntries(RenderTargetCatalog& catalog) const
{
    catalog.entries.push_back({
        .label            = "Deferred GBuffer",
        .owner            = RenderTargetCatalog::Entry::EOwner::DeferredGBuffer,
        .colorFormats     = buildGBufferSnapshotFormats().colorFormats,
        .depthFormat      = buildGBufferSnapshotFormats().depthFormat,
        .extent           = _gBufferRTSpec.extent,
        .frameBufferCount = 1,
    });
    catalog.entries.push_back({
        .label            = "Deferred View",
        .owner            = RenderTargetCatalog::Entry::EOwner::DeferredView,
        .colorFormats     = buildViewSnapshotFormats().colorFormats,
        .depthFormat      = buildViewSnapshotFormats().depthFormat,
        .extent           = _viewRTSpec.extent,
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

void DeferredRenderPipeline::appendTargetRequests(
    const SceneRenderPlan& plan,
    std::vector<ViewTargetRequest>& out) const
{
    const RenderPipelineSettings settings = resolveSettings();
    const auto gBufferFormats = buildGBufferSnapshotFormats();
    for (const SceneViewTask& task : plan.viewTasks) {
        if (task.desc.viewId == 0 || !task.output.hasExtent()) {
            continue;
        }
        ViewTargetRequest request{
            .viewId   = task.desc.viewId,
            .pipeline = ERenderPipelineKind::Deferred,
            .extent   = task.output.extent,
            .attachments = {
                {EViewAttachment::SceneColor, _viewColorFormat, _viewRTSpec.attachments.colorAttach.front().usage},
                {EViewAttachment::SceneDepth, _sharedDepthFormat, _gBufferRTSpec.attachments.depthAttach->usage, ESampleCount::Sample_1, true},
                {EViewAttachment::DisplayColor, POSTPROCESS_COLOR_FORMAT, EImageUsage::ColorAttachment | EImageUsage::Sampled | EImageUsage::TransferSrc},
                {EViewAttachment::EntityId, EFormat::R32_UINT, EImageUsage::ColorAttachment | EImageUsage::TransferSrc},
                {EViewAttachment::GBuffer0, gBufferFormats.colorFormats[0], _gBufferRTSpec.attachments.colorAttach[0].usage},
                {EViewAttachment::GBuffer1, gBufferFormats.colorFormats[1], _gBufferRTSpec.attachments.colorAttach[1].usage},
                {EViewAttachment::GBuffer2, gBufferFormats.colorFormats[2], _gBufferRTSpec.attachments.colorAttach[2].usage},
                {EViewAttachment::GBuffer3, gBufferFormats.colorFormats[3], _gBufferRTSpec.attachments.colorAttach[3].usage},
            },
        };
        if (settings.bSSAOEnabled) {
            request.attachments.push_back({EViewAttachment::SSAO, SSAOStage::AO_FORMAT, EImageUsage::ColorAttachment | EImageUsage::Sampled});
        }
        if (isGradingEnabled() && settings.postProcessing.bEnableBloom) {
            request.attachments.push_back({EViewAttachment::BloomExtract, BloomPostprocessing::BLOOM_FORMAT, EImageUsage::ColorAttachment | EImageUsage::Sampled});
            request.attachments.push_back({EViewAttachment::BloomBlur, BloomPostprocessing::BLOOM_FORMAT, EImageUsage::ColorAttachment | EImageUsage::Sampled});
            request.attachments.push_back({EViewAttachment::BloomComposite, BloomPostprocessing::BLOOM_FORMAT, EImageUsage::ColorAttachment | EImageUsage::Sampled});
        }
        out.push_back(std::move(request));
    }
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
    case RenderTargetCatalog::Entry::EOwner::DeferredView:
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
    case RenderTargetCatalog::Entry::EOwner::DeferredView:
        if (attachmentIndex >= _viewRTSpec.attachments.colorAttach.size()) {
            return false;
        }
        if (_viewRTSpec.attachments.colorAttach[attachmentIndex].format != format) {
            _viewRTSpec.attachments.colorAttach[attachmentIndex].format = format;
            bFormatChanged                                                  = true;
        }
        if (bFormatChanged) {
            _viewColorFormat = _viewRTSpec.attachments.colorAttach[0].format;
            markPendingResourceRefresh(EDeferredPendingResourceRefresh::ViewAttachments);
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

void DeferredRenderPipeline::requestShadowResourceRefresh()
{
    markPendingResourceRefresh(EDeferredPendingResourceRefresh::ShadowResources);
}

void DeferredRenderPipeline::applyPendingResourceRefreshes()
{
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

    if (hasPendingResourceRefresh(EDeferredPendingResourceRefresh::ViewAttachments)) {
        clearPendingResourceRefresh(EDeferredPendingResourceRefresh::ViewAttachments);
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
    _debugRenderSystem            = desc.debugRenderSystem;
    _pendingSettings.reset();
    _pendingResourceRefreshMask   = 0;
    if (_shadowSettings) {
        _frameShadowSettings = *_shadowSettings;
    }
    loadPersistentSettings();
    YA_CORE_ASSERT(_render, "DeferredRenderPipeline requires a valid render backend");
    _defaultSkyboxMesh = PrimitiveMeshCache::get().getMesh(EPrimitiveGeometry::Cube);
    YA_CORE_ASSERT(_defaultSkyboxMesh != nullptr, "DeferredRenderPipeline requires default skybox cube mesh");
    resolveRuntimeFormats();

    Extent2D extent{
        .width  = static_cast<uint32_t>(desc.viewWidth),
        .height = static_cast<uint32_t>(desc.viewHeight),
    };

    initRenderTargetSpecs(extent);
    _entityIdPass.init(_render, EFormat::R32_UINT, _sharedDepthFormat);
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
    _ssaoStage->init(_render, _frameResources->getSSAOFrameDSL());
    _ssaoStage->setSettings(_ssaoRadius, _ssaoBias, _ssaoPower, _ssaoIntensity, _bReverseViewportY);

    _lightStage = ya::makeShared<LightStage>();
    _lightStage->setup(LightStage::SharedInputs{
        .frameAndLightDSL = _frameResources->getFrameAndLightDSL(),
    });
    _lightStage->setEnvironmentLightingInput(LightStage::EnvironmentLightingInput{
        .environmentLightingDSL = _environmentLightingDSL,
    });
    _lightStage->init(_render);
    _lightStage->setIBLSettings(_bEnablePBRDiffuseIBL, _bEnablePBRSpecularIBL);
    syncShadowSettings();

    _overlayStage = ya::makeShared<ViewOverlayStage>();
    _overlayStage->setDebugRenderSystem(_debugRenderSystem);
    _overlayStage->init(_render, _frameResources->getSkyboxFrameDSL());

    refreshGBufferStageState();
    refreshViewStageState();
}

void DeferredRenderPipeline::shutdown()
{
    _entityIdPass.destroy();
    _postProcessStage.shutdown();

    _pendingResourceRefreshMask      = 0;
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
    _debugRenderSystem = nullptr;
    _render                       = nullptr;
}


// ═══════════════════════════════════════════════════════════════════════
// Family graph
// ═══════════════════════════════════════════════════════════════════════

namespace
{

RenderPipelineFrameContext makeDeferredViewFrameContext(const ViewFamilyRecordContext& ctx,
                                                        const SceneViewRecording& recording)
{
    // The View's own declaration and prepared data are the camera; there is no
    // host packet to copy and override field by field.
    const Extent2D viewExtent = recording.task ? recording.task->output.extent : Extent2D{};
    return RenderPipelineFrameContext{
        .cmdBuf                  = ctx.cmdBuf,
        .frame                   = ctx.frame,
        .submission              = ctx.submission,
        .view                    = RenderViewRecordingContext{
            .task           = recording.task,
            .frameData      = recording.frameData,
            .viewExtent = viewExtent,
        },
        .derivedScene            = recording.task ? recording.task->desc.scene : nullptr,
    };
}

struct DeferredFamilyViewBranch
{
    RenderPipelineFrameContext            frame{};
    RenderStageContext                    stageCtx{};
    /// The View's own shadow preparation result, carried from prepare to the
    /// graph build so the shadow stage needs no "current View".
    ShadowPreparedView                    shadowPrepared{};
    uint32_t                              vpW = 0;
    uint32_t                              vpH = 0;
    ViewOverlayStage::FrameInputs     overlayInputs{};
    EnvironmentLightingSceneResources     environmentLighting{};
    DescriptorSetHandle                   environmentLightingDS{};
    FrameContext                          postContext{};
    DeferredFrameGraphResources           graphResources{};
    ViewTargetLease                       targets{};
};

} // namespace

void DeferredRenderPipeline::beginSubmission()
{
    // Submission-scoped warmup: pending settings land once, then every stage's
    // PSO beginFrame runs once per submission instead of once per View.
    applyPendingSettings();
    applyPendingResourceRefreshes();
    if (_gBufferStage) {
        _gBufferStage->beginFrame();
    }
    _postProcessStage.beginFrame();
    if (_bEnableSSAO && _ssaoStage) {
        _ssaoStage->beginFrame();
    }
    if (_lightStage) {
        _lightStage->beginFrame();
    }
    if (_overlayStage) {
        _overlayStage->beginFrame();
    }
}

ViewFamilyRenderResult DeferredRenderPipeline::recordFamily(const ViewFamilyRecordContext& ctx)
{
    YA_PROFILE_FUNCTION();

    ViewFamilyRenderResult result;
    if (ctx.family) {
        result.key = ctx.family->key;
    }
    if (!ctx.cmdBuf || !ctx.submission || !ctx.submission->isRecording()) {
        return result;
    }

    ctx.cmdBuf->debugBeginLabel("Deferred Family");
    YA_PERF_SCOPE(perf::sample::deferredTick(), perf::metric::cpuTimeMs(), perf::domain::render());

    // A family exists because a Scene has content and a View declared it, so an
    // empty family is not a tick to synthesize a View for: there is no View id,
    // no output identity and no declared geometry to record into.
    std::vector<SceneViewRecording> recordings = ctx.views;
    if (recordings.empty()) {
        return result;
    }

    RenderGraph graph;
    std::vector<DeferredFamilyViewBranch> liveBranches;
    liveBranches.reserve(recordings.size());
    std::optional<RGPassHandle> familyPredecessor;
    bool preparedSkinning = false;

    for (const SceneViewRecording& recording : recordings) {
        DeferredFamilyViewBranch branch;
        branch.frame = makeDeferredViewFrameContext(ctx, recording);
        if (shouldSkipView(branch.frame)) {
            continue;
        }
        branch.targets = ctx.targets && recording.task
            ? ctx.targets->lease(recording.task->desc.viewId)
            : ViewTargetLease{};
        if (!branch.targets || !ctx.submission->retain(branch.targets.allocation)) {
            YA_CORE_ERROR("Deferred View {} has no prepared target lease",
                          recording.task ? recording.task->desc.viewId : 0);
            continue;
        }

        beginViewRecording(branch.frame, branch.stageCtx, branch.vpW, branch.vpH);
        syncFrameSettings(branch.frame);
        applyPendingResourceRefreshes();
        branch.shadowPrepared = prepareShadowPass(branch.frame, branch.stageCtx);

        if (!preparedSkinning) {
            RenderViewRecordingContext view = branch.frame.view;
            if (!_frameResources->prepareSkinning(*branch.frame.submission, view)) {
                continue;
            }
            preparedSkinning = true;
        }

        branch.overlayInputs = buildOverlayFrameInputs(
            branch.frame, branch.environmentLighting, branch.environmentLightingDS);
        liveBranches.push_back(std::move(branch));
        DeferredFamilyViewBranch& live = liveBranches.back();
        if (!appendDeferredViewToGraph(
                graph,
                live.frame,
                live.stageCtx,
                live.shadowPrepared,
                live.vpW,
                live.vpH,
                live.overlayInputs,
                live.environmentLighting,
                live.environmentLightingDS,
                live.postContext,
                live.graphResources,
                live.targets,
                familyPredecessor)) {
            liveBranches.pop_back();
            continue;
        }
        familyPredecessor = graph.lastPassHandle();
    }

    if (liveBranches.empty()) {
        ctx.cmdBuf->debugEndLabel();
        return result;
    }

    YA_CORE_ASSERT(_graphExecutor != nullptr, "DeferredRenderPipeline graph executor is not initialized");
    RGCompiledGraph compiled{};
    RenderGraphExecutionResult execution;
    if (!_graphExecutor->prepare(graph, compiled, &execution)) {
        ctx.cmdBuf->debugEndLabel();
        return result;
    }

    for (const DeferredFamilyViewBranch& branch : liveBranches) {
        const uint64_t viewId = branch.frame.view.task ? branch.frame.view.task->desc.viewId : 0;
        auto output = collectViewOutput(
            execution, branch.graphResources, branch.frame.view.task, viewId);
        output.targets = branch.targets.allocation;
        output.allocationGeneration = branch.targets.allocation ? branch.targets.allocation->generation : 0;
        result.views.push_back(std::move(output));
    }

    if (!_graphExecutor->executeCompiled(graph, compiled, *ctx.cmdBuf)) {
        result.views.clear();
    }
    else {
        result.topology = graph.describeCompiledTopology(compiled);
    }

    ctx.cmdBuf->debugEndLabel();
    return result;
}

bool DeferredRenderPipeline::shouldSkipView(const RenderPipelineFrameContext& frame) const
{
    YA_CORE_ASSERT(frame.cmdBuf, "DeferredRenderPipeline requires a command buffer");
    return frame.view.viewExtent.width == 0 || frame.view.viewExtent.height == 0 ||
           !frame.view.frameData;
}

void DeferredRenderPipeline::beginViewRecording(const RenderPipelineFrameContext& frame, RenderStageContext& stageCtx, uint32_t& vpW, uint32_t& vpH)
{
    captureShadowSettings(frame);

    vpW = frame.view.viewExtent.width;
    vpH = frame.view.viewExtent.height;

    _lastPointLightCount = frame.view.frameData->numPointLights;
    _lastDrawCount       = static_cast<uint32_t>(frame.view.frameData->totalDrawCount());

    stageCtx = RenderStageContext{
        .cmdBuf         = frame.cmdBuf,
        .frameData      = frame.view.frameData,
        .flightIndex    = frame.frame ? frame.frame->flightIndex : 0,
        .hostTick       = frame.frame ? frame.frame->hostTick : 0,
        .deltaTime      = frame.frame ? frame.frame->deltaTime : 0.0f,
        .timeSeconds    = frame.frame ? frame.frame->elapsedTimeSeconds : 0.0f,
        .viewExtent = {.width = vpW, .height = vpH},
        .derivedScene   = frame.derivedScene,
    };
}

void DeferredRenderPipeline::captureShadowSettings(const RenderPipelineFrameContext& frame)
{
    if (frame.frame && frame.frame->shadowSettings) {
        _frameShadowSettings = *frame.frame->shadowSettings;
    }
    else if (_shadowSettings) {
        _frameShadowSettings = *_shadowSettings;
    }
}

ViewOverlayStage::FrameInputs DeferredRenderPipeline::buildOverlayFrameInputs(
    const RenderPipelineFrameContext& frame,
    EnvironmentLightingSceneResources& environmentLighting,
    DescriptorSetHandle& environmentLightingDS) const
{
    // The View's own scene resources, resolved before recording began: this pass
    // binds the Scene its View declared instead of asking which one is current.
    Scene* activeScene = frame.derivedScene;
    const RenderViewSceneResources& sceneResources =
        frame.view.frameData ? frame.view.frameData->sceneResources : RenderViewSceneResources{};
    environmentLighting   = sceneResources.environmentLightingResources;
    environmentLightingDS = sceneResources.environmentLightingDescriptorSet;

    ViewOverlayStage::FrameInputs frameInputs{};
    if (!_overlayStage) {
        return frameInputs;
    }

    auto* envProcessor = sceneResources.environmentLighting;

    if (activeScene) {
        const float viewHeight = static_cast<float>(frame.view.viewExtent.height);
        if (viewHeight > 0.0f) {
            for (const auto& [entity, billboard, transform] : activeScene->getRegistry().view<BillboardComponent, TransformComponent>().each()) {
                if (!billboard.bVisible) {
                    continue;
                }

                // Same gate the mesh buckets use: the component says which
                // feature it belongs to, the view says which features it draws.
                // A generated editor companion therefore disappears from a game
                // view without the component knowing about views at all.
                if (!rendersFeature(billboard.features, frame.view.frameData->viewFeatures)) {
                    continue;
                }

                const glm::vec3 worldCenter = transform.getWorldPosition();
                const float distance        = glm::length(frame.view.frameData->cameraPos - worldCenter);
                if (distance <= std::numeric_limits<float>::epsilon()) {
                    continue;
                }

                const float screenSizePixels = std::max(billboard.screenSizePixels, 1.0f);
                const float scaleFactor      = screenSizePixels / viewHeight;
                const float size             = std::max(billboard.minWorldScale, scaleFactor * distance * 2.0f);

                ViewOverlayStage::FrameInputs::BillboardInput input{};
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

    if (activeScene && envProcessor) {
        const auto* skyboxState = envProcessor->findFirstSceneSkyboxState(activeScene);
        if (skyboxState && skyboxState->hasRenderableCubemap()) {
            frameInputs.skybox.descriptorSet = sceneResources.skyboxDescriptorSet;
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

    return frameInputs;
}

RenderViewOutput DeferredRenderPipeline::collectViewOutput(
    const RenderGraphExecutionResult& result,
    const DeferredFrameGraphResources& graphResources,
    const SceneViewTask* task,
    uint64_t viewId) const
{
    RenderViewOutput output;
    if (task) {
        output.desc = task->output;
    }
    output.desc.viewId = viewId != 0 ? viewId : (task ? task->desc.viewId : 0);
    if (!output.desc.hasExtent()) {
        output.desc.extent = task ? task->output.extent : Extent2D{};
    }

    output.color    = result.getExportedTextureShared(makeViewGraphName(deferred_graph_exports::viewColor, viewId));
    output.depth    = result.getExportedTextureShared(makeViewGraphName(deferred_graph_exports::gBufferDepth, viewId));
    output.entityId = result.getExportedTextureShared(makeViewGraphName(deferred_graph_exports::entityId, viewId));
    output.ssao     = graphResources.textures.ssao.has_value()
        ? result.getExportedTextureShared(makeViewGraphName(deferred_graph_exports::ssao, viewId))
        : nullptr;
    for (uint32_t attachmentIndex = 0;
         attachmentIndex < std::size(deferred_graph_exports::gBufferColor);
         ++attachmentIndex) {
        output.gBufferColors[attachmentIndex] = result.getExportedTextureShared(
            makeViewGraphName(deferred_graph_exports::gBufferColor[attachmentIndex], viewId));
    }
    output.bloomExtract = result.getExportedTextureShared(makeViewGraphName(BloomPostprocessing::kExtractExportName, viewId));
    output.bloomBlur    = result.getExportedTextureShared(makeViewGraphName(BloomPostprocessing::kBlurPongExportName, viewId));
    if (!output.bloomBlur) {
        output.bloomBlur = result.getExportedTextureShared(makeViewGraphName(BloomPostprocessing::kBlurPingExportName, viewId));
    }
    output.bloomComposite = result.getExportedTextureShared(makeViewGraphName(BloomPostprocessing::kOutputExportName, viewId));
    const auto postprocess = graphResources.textures.postprocessOutput.has_value()
        ? result.getExportedTextureShared(makeViewGraphName(PostProcessingStage::kOutputExportName, viewId))
        : nullptr;
    // The display image is the finalize output, always: finalize is the pass
    // that encodes the renderer's linear color, so it runs every frame and the
    // display image never falls back to the raw color attachment. A fallback
    // here is what let a presentation pass rescue an unencoded image by grading
    // it itself, which double-graded the normal path.
    output.display = postprocess;
    if (output.color) {
        output.desc.colorFormat = output.color->getFormat();
        if (!output.desc.hasExtent()) {
            output.desc.extent = output.color->getExtent();
        }
    }
    if (output.depth) {
        output.desc.depthFormat = output.depth->getFormat();
    }
    return output;
}

DeferredAttachmentFormats DeferredRenderPipeline::buildGBufferSnapshotFormats() const
{
    return buildDeferredFormatsFromSpec(_gBufferRTSpec);
}

DeferredAttachmentFormats DeferredRenderPipeline::buildViewSnapshotFormats() const
{
    DeferredAttachmentFormats formats = buildDeferredFormatsFromSpec(_viewRTSpec);
    formats.depthFormat               = buildGBufferSnapshotFormats().depthFormat;
    return formats;
}

EFormat::T DeferredRenderPipeline::getViewColorFormat() const
{
    if (_viewRTSpec.attachments.colorAttach.empty()) {
        return EFormat::Undefined;
    }
    return _viewRTSpec.attachments.colorAttach.front().format;
}

EFormat::T DeferredRenderPipeline::getViewDepthFormat() const
{
    return buildGBufferSnapshotFormats().depthFormat.value_or(EFormat::Undefined);
}

void DeferredRenderPipeline::refreshGBufferStageState()
{
    if (_ssaoStage) {
        _ssaoStage->refreshPipelineFormat();
    }

    if (_gBufferStage) {
        _gBufferStage->refreshPipelineFormats(buildGBufferSnapshotFormats());
    }

    if (_lightStage) {
        _lightStage->setup(LightStage::SharedInputs{
            .frameAndLightDSL = _frameResources ? _frameResources->getFrameAndLightDSL() : nullptr,
        });
    }
}

void DeferredRenderPipeline::refreshViewStageState()
{
    if (_lightStage) {
        _lightStage->refreshPipelineFormats(buildViewSnapshotFormats());
    }

    if (_overlayStage) {
        _overlayStage->refreshPipelineFormats(buildViewSnapshotFormats());
    }
}

void DeferredRenderPipeline::syncFrameSettings(const RenderPipelineFrameContext& frame)
{
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
}

ShadowPreparedView DeferredRenderPipeline::prepareShadowPass(const RenderPipelineFrameContext& frame,
                                                             RenderStageContext&               stageCtx)
{
    const auto shadowSettings = currentShadowSettings();
    if (_shadowStage && shadowSettings.isEnabled()) {
        _shadowStage->applySettings(shadowSettings);
        YA_PERF_SCOPE(perf::sample::deferredShadow(), perf::metric::cpuTimeMs(), perf::domain::render());
        if (!frame.submission || !frame.submission->isRecording()) {
            YA_CORE_ERROR("Deferred shadow pass requires a recording submission");
            return {};
        }

        RenderViewRecordingContext view = frame.view;
            if (view.viewExtent.width == 0 && view.viewExtent.height == 0) {
            view.viewExtent = stageCtx.viewExtent;
        }
        return _shadowStage->prepareView(*frame.submission, view);
    }

    PerfState::get().clearMetric(perf::sample::deferredShadow(), perf::metric::cpuTimeMs());
    return {};
}

bool DeferredRenderPipeline::appendDeferredViewToGraph(RenderGraph& graph,
                                                       const RenderPipelineFrameContext& frame,
                                                       RenderStageContext& stageCtx,
                                                       const ShadowPreparedView& shadowPrepared,
                                                       uint32_t vpW,
                                                       uint32_t vpH,
                                                       ViewOverlayStage::FrameInputs& overlayInputs,
                                                       EnvironmentLightingSceneResources& environmentLighting,
                                                       DescriptorSetHandle environmentLightingDS,
                                                 FrameContext& postContext,
                                                 DeferredFrameGraphResources& graphResources,
                                                 const ViewTargetLease& targets,
                                                 std::optional<RGPassHandle> familyPredecessor)
{
    YA_CORE_ASSERT(_frameResources != nullptr, "Deferred pipeline frame resources are not initialized");
    if (!frame.submission || !frame.submission->isRecording()) {
        return false;
    }
    RenderSubmission& submission = *frame.submission;

    RenderViewRecordingContext view = frame.view;
    if (view.viewExtent.width == 0 && view.viewExtent.height == 0) {
        view.viewExtent = stageCtx.viewExtent;
    }

    const bool bUseSSAO = _bEnableSSAO && _ssaoStage;
    DeferredFrameResourceSet::SSAOFrameData   ssaoStorage{};
    DeferredFrameResourceSet::SkyboxFrameData skyboxStorage{};
    const DeferredFrameResourceSet::SSAOFrameData*   ssao   = nullptr;
    const DeferredFrameResourceSet::SkyboxFrameData* skybox = nullptr;
    if (bUseSSAO) {
        ssaoStorage = _ssaoStage->buildFrameData(stageCtx);
        ssao        = &ssaoStorage;
    }
    if (_overlayStage) {
        skyboxStorage = _overlayStage->buildSkyboxFrameData(stageCtx);
        skybox        = &skyboxStorage;
    }

    const auto* viewBinding = _frameResources->beginView(submission, view, ssao, skybox);
    if (!viewBinding) {
        return false;
    }
    auto* viewResources = _frameResources->mutableViewResources(submission.flightIndex(), view.viewSlot);
    if (!viewResources) {
        return false;
    }
    const uint32_t alignment = std::max(_render ? _render->getUniformBufferOffsetAlignment() : 1u, 1u);
    allocateDeferredViewPassResources(
        submission,
        _render,
        alignment,
        view.frameData,
        _ssaoStage.get(),
        _lightStage.get(),
        &_entityIdPass,
        _overlayStage.get(),
        &_postProcessStage,
        &overlayInputs,
        *viewResources);

    overlayInputs.skybox.frameDescriptorSet = viewBinding->skyboxFrameDescriptorSet;
    _gBufferStage->prepare(stageCtx);

    postContext = FrameContext{
        .view           = frame.view.frameData->view,
        .projection     = frame.view.frameData->projection,
        .viewProjection = frame.view.frameData->viewProjection,
        .cameraPos      = frame.view.frameData->cameraPos,
        .extent         = {.width = vpW, .height = vpH},
    };
    graphResources = {};
    RenderTargetCreateInfo viewViewportSpec = _viewRTSpec;
    RenderTargetCreateInfo viewGBufferSpec  = _gBufferRTSpec;
    const Extent2D viewExtent{vpW, vpH};
    if (viewExtent.width > 0 && viewExtent.height > 0) {
        viewViewportSpec.extent = viewExtent;
        viewGBufferSpec.extent  = viewExtent;
    }
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
            .frameBinding             = viewBinding,
            .frame                    = &frame,
            .gBufferRTSpec            = &viewGBufferSpec,
            .viewRTSpec           = &viewViewportSpec,
            .overlayInputs            = &overlayInputs,
            .environmentLighting      = &environmentLighting,
            .environmentLightingDS    = environmentLightingDS,
            .postContext              = &postContext,
            .viewExtent           = viewViewportSpec.extent,
            .shadowPrepared           = shadowPrepared,
            .bUseSSAO                 = bUseSSAO,
            .bReverseViewportY        = _bReverseViewportY,
            .bPostprocessOutputIsSRGB = EFormat::isSRGB(POSTPROCESS_COLOR_FORMAT),
            .viewId                   = frame.view.task ? frame.view.task->desc.viewId : 0,
        .viewResources            = viewResources,
        .targets                  = &targets,
            .familyPredecessor        = familyPredecessor,
        });
    return true;
}

// ═══════════════════════════════════════════════════════════════════════
// Depth Copy
// ═══════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════
// Viewport Pass
// ═══════════════════════════════════════════════════════════════════════

} // namespace ya
