#include "Runtime/Rendering/Forward/ForwardRenderPipeline.h"

#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Render/Core/Buffer.h"
#include "Core/Profiling/Profiling.h"
#include "Render/Core/RenderGraphImportUtils.h"
#include "Render/Core/RenderingInfoUtils.h"
#include "Render/Core/Sampler.h"
#include "Runtime/Rendering/Forward/ForwardFrameGraphResources.h"
#include <glm/gtc/matrix_transform.hpp>

namespace ya
{

namespace
{

RenderAttachmentFormats buildForwardViewportFormats(const RenderTargetCreateInfo& spec)
{
    RenderAttachmentFormats formats{};
    formats.colorFormats.reserve(spec.attachments.colorAttach.size());
    for (const auto& desc : spec.attachments.colorAttach) {
        formats.colorFormats.push_back(desc.format);
    }

    if (spec.attachments.depthAttach.has_value()) {
        formats.depthFormat = spec.attachments.depthAttach->format;
    }

    return formats;
}

RenderTargetCreateInfo buildForwardViewportRenderTargetSpec(Extent2D extent, EFormat::T colorFormat, EFormat::T depthFormat)
{
    return RenderTargetCreateInfo{
        .label            = "Viewport RenderTarget",
        .renderingMode    = ERenderingMode::DynamicRendering,
        .bSwapChainTarget = false,
        .extent           = extent,
        .frameBufferCount = 1,
        .attachments      = {
            .colorAttach = {
                AttachmentDescription{
                    .index         = 0,
                    .format        = colorFormat,
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
                .format         = depthFormat,
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
    };
}

ImageViewCreateInfo makeForwardViewportViewDesc(std::string_view label,
                                                EFormat::T       format,
                                                uint32_t         layerCount)
{
    const bool bCube    = layerCount == 6;
    const bool bArray2D = layerCount > 1 && !bCube;
    return ImageViewCreateInfo{
        .label          = std::string(label),
        .viewType       = bCube ? EImageViewType::ViewCube : bArray2D ? EImageViewType::View2DArray : EImageViewType::View2D,
        .aspectFlags    = EFormat::isDepthStencilFormat(format) ? EImageAspect::DepthStencil :
                          EFormat::isDepthFormat(format) ? EImageAspect::Depth : EImageAspect::Color,
        .baseMipLevel   = 0,
        .levelCount     = 1,
        .baseArrayLayer = 0,
        .layerCount     = layerCount,
    };
}

std::shared_ptr<RenderImage> createForwardViewportAttachment(IRender& render,
                                                             const AttachmentDescription& attachment,
                                                             Extent2D extent,
                                                             uint32_t layerCount,
                                                             std::string label)
{
    return createRenderImage(
        *render.getResourceFactory(),
        RenderImageDesc{
            .image = ImageCreateInfo{
                .label       = label,
                .format      = attachment.format,
                .extent      = {.width = extent.width, .height = extent.height, .depth = 1},
                .mipLevels   = 1,
                .arrayLayers = layerCount,
                .samples     = attachment.samples,
                .usage       = attachment.usage,
                .initialLayout = attachment.initialLayout,
                .flags       = attachment.imageCreateFlags,
            },
            .defaultView = makeForwardViewportViewDesc(std::format("{}.DefaultView", label), attachment.format, layerCount),
        });
}

ForwardViewportResources buildForwardViewportResources(IRender& render, const RenderTargetCreateInfo& spec)
{
    ForwardViewportResources resources{};
    resources.extent = spec.extent;

    if (!spec.attachments.colorAttach.empty()) {
        resources.colorOwner = createForwardViewportAttachment(
            render,
            spec.attachments.colorAttach[0],
            spec.extent,
            spec.layerCount,
            std::format("{}.Color0", spec.label));
    }

    if (spec.attachments.depthAttach.has_value()) {
        resources.depthOwner = createForwardViewportAttachment(
            render,
            *spec.attachments.depthAttach,
            spec.extent,
            spec.layerCount,
            std::format("{}.Depth", spec.label));
    }

    if (spec.attachments.resolveAttach.has_value()) {
        resources.resolveOwner = createForwardViewportAttachment(
            render,
            *spec.attachments.resolveAttach,
            spec.extent,
            spec.layerCount,
            std::format("{}.Resolve", spec.label));
    }

    resources.syncRawViews();
    return resources;
}

RGImportedTextureDesc makeForwardViewportImportedDesc(const RenderImage& image,
                                                      std::string_view   label,
                                                      EImageLayout::T    finalLayout)
{
    return makeImportedTextureDesc(image, label, finalLayout);
}

} // namespace

void ForwardRenderPipeline::appendRenderTargetEntries(RenderTargetCatalog& catalog) const
{
    catalog.entries.push_back({
        .label            = "Forward Viewport",
        .owner            = RenderTargetCatalog::Entry::EOwner::ForwardViewport,
        .colorFormats     = _viewportFormats.colorFormats,
        .depthFormat      = _viewportFormats.depthFormat,
        .colorAttachments = {_viewportResources.colorOwner},
        .depthAttachment  = _viewportResources.depthOwner,
        .extent           = _viewportResources.extent,
        .frameBufferCount = 1,
    });
    catalog.entries.push_back({
        .label               = "Forward Shadow",
        .owner               = RenderTargetCatalog::Entry::EOwner::ForwardShadow,
        .depthFormat         = _shadowResources.depthFormat,
        .depthAttachmentView = _shadowResources.directionalDepthIV,
        .extent              = _shadowResources.extent,
        .frameBufferCount    = 1,
    });
}

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
    _graphExecutor          = _render ? std::make_unique<RenderGraphExecutor>(*_render->getResourceFactory()) : nullptr;
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
}

void ForwardRenderPipeline::initViewportResources(const InitDesc& desc)
{
    _viewportRTSpec = buildForwardViewportRenderTargetSpec(
        {.width = static_cast<uint32_t>(desc.windowW), .height = static_cast<uint32_t>(desc.windowH)},
        VIEWPORT_COLOR_FORMAT,
        DEPTH_FORMAT);
    recreateViewportResources();
    refreshViewportSnapshot();
}

void ForwardRenderPipeline::initPostProcessResources(const InitDesc& desc)
{
    _postProcessStage.init(PostProcessingStage::InitDesc{
        .render      = _render,
        .colorFormat = POSTPROCESS_COLOR_FORMAT,
        .width       = static_cast<uint32_t>(desc.windowW),
        .height      = static_cast<uint32_t>(desc.windowH),
    });
    _postProcessStage.setGraphExecutor(_graphExecutor.get());
    _deleter.push("PostProcessStage", [this](void*)
                  { _postProcessStage.shutdown(); });
}

void ForwardRenderPipeline::initShadowResources()
{
    const uint32_t shadowResolution = std::max(currentShadowSettings().resolution, 1u);

    _shadowResources.init(_render, ShadowMapResourceDesc{
        .imageLabel      = "Shadow Map Depth",
        .samplerLabel    = "shadow",
        .viewLabelPrefix = "Shadow Map",
        .extent          = {.width = shadowResolution, .height = shadowResolution},
        .depthFormat     = _shadowDepthFormat,
    });

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

    rebuildShadowViews();

    _deleter.push("Shadow ImageViews", [this](void*)
                  { _shadowResources.destroy(); });
}

void ForwardRenderPipeline::initStageResources()
{
    _frameResources = ya::makeShared<ForwardFrameResourceSet>();
    _frameResources->init(_render);

    _shadowStage = ya::makeShared<ShadowStage>();
    _shadowStage->init(_render);
    if (_shadowResources.depthImage) {
        _shadowStage->refreshShadowResources(
            _shadowResources.depthImage,
            _shadowResources.depthFormat,
            _shadowResources.extent);
    }

    PipelineRenderingInfo viewportPRI{
        .label                  = "Forward Viewport",
        .colorAttachmentFormats = _viewportFormats.colorFormats,
        .depthAttachmentFormat  = _viewportFormats.depthFormat.value_or(EFormat::Undefined),
    };
    _viewportStage = ya::makeShared<ForwardViewportStage>();
    _viewportStage->initWithDesc(ForwardViewportStage::InitDesc{
        .render                             = _render,
        .renderPass                         = nullptr,
        .pipelineRenderingInfo              = viewportPRI,
        .skinningDSL                        = _frameResources ? _frameResources->getSkinningDSL() : nullptr,
        .pbrFrameDSL                        = _frameResources ? _frameResources->getPBRFrameDSL() : nullptr,
        .phongFrameDSL                      = _frameResources ? _frameResources->getPhongFrameDSL() : nullptr,
        .unlitFrameDSL                      = _frameResources ? _frameResources->getUnlitFrameDSL() : nullptr,
        .skyboxFrameDSL                     = _frameResources ? _frameResources->getSkyboxFrameDSL() : nullptr,
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
    YA_PROFILE_FUNCTION();

    if (shouldSkipTick(frame)) {
        return;
    }

    RenderStageContext stageCtx{};
    {
        YA_PROFILE_SCOPE("ForwardPipeline/BeginTick");
        beginTick(frame, stageCtx);
    }
    {
        YA_PROFILE_SCOPE("ForwardPipeline/ShadowPass");
        executeShadowPass(stageCtx);
    }
    {
        YA_PROFILE_SCOPE("ForwardPipeline/ViewportPass");
        executeViewportPass(frame, stageCtx);
    }
    {
        YA_PROFILE_SCOPE("ForwardPipeline/FinalizeViewport");
        finalizeViewportPass(frame.cmdBuf);
    }
}

bool ForwardRenderPipeline::shouldSkipTick(const RenderPipelineFrameContext& frame) const
{
    YA_CORE_ASSERT(frame.cmdBuf, "ForwardRenderPipeline requires command buffer");
    return frame.viewportRect.extent.x <= 0 || frame.viewportRect.extent.y <= 0;
}

void ForwardRenderPipeline::beginTick(const RenderPipelineFrameContext& frame, RenderStageContext& stageCtx)
{
    _postProcessStage.beginFrame();
    captureShadowSettings(frame);
    syncFrameSettings(frame);
    applyPendingResourceRefreshes();

    stageCtx = RenderStageContext{
        .cmdBuf         = frame.cmdBuf,
        .frameData      = frame.frameData,
        .flightIndex    = frame.flightIndex,
        .deltaTime      = frame.deltaTime,
        .viewportExtent = _viewportResources.extent,
    };
}

bool ForwardRenderPipeline::setRenderTargetColorFormat(RenderTargetCatalog::Entry::EOwner owner,
                                                       uint32_t                                 attachmentIndex,
                                                       EFormat::T                               format)
{
    bool bFormatChanged = false;
    switch (owner) {
    case RenderTargetCatalog::Entry::EOwner::ForwardViewport:
        if (attachmentIndex < _viewportRTSpec.attachments.colorAttach.size()) {
            auto& colorDesc = _viewportRTSpec.attachments.colorAttach[attachmentIndex];
            bFormatChanged  = colorDesc.format != format;
            colorDesc.format = format;
        }
        break;
    default:
        return false;
    }

    if (bFormatChanged) {
        markPendingResourceRefresh(EForwardPendingResourceRefresh::AttachmentFormat);
    }
    return true;
}

bool ForwardRenderPipeline::setRenderTargetDepthFormat(
    RenderTargetCatalog::Entry::EOwner owner,
    EFormat::T format)
{
    if (owner != RenderTargetCatalog::Entry::EOwner::ForwardShadow) {
        return false;
    }
    if (_shadowDepthFormat != format) {
        _shadowDepthFormat = format;
        requestShadowResourceRefresh();
    }
    return true;
}

void ForwardRenderPipeline::markPendingResourceRefresh(EForwardPendingResourceRefresh refresh)
{
    _pendingResourceRefreshMask |= static_cast<uint32_t>(refresh);
}

bool ForwardRenderPipeline::hasPendingResourceRefresh(EForwardPendingResourceRefresh refresh) const
{
    return (_pendingResourceRefreshMask & static_cast<uint32_t>(refresh)) != 0;
}

void ForwardRenderPipeline::clearPendingResourceRefresh(EForwardPendingResourceRefresh refresh)
{
    _pendingResourceRefreshMask &= ~static_cast<uint32_t>(refresh);
}

void ForwardRenderPipeline::requestViewportResize(Extent2D extent)
{
    if (extent.width == 0 || extent.height == 0) {
        return;
    }

    _pendingViewportExtent = extent;
    markPendingResourceRefresh(EForwardPendingResourceRefresh::ViewportResize);
}

void ForwardRenderPipeline::requestShadowResourceRefresh()
{
    markPendingResourceRefresh(EForwardPendingResourceRefresh::ShadowResources);
}

void ForwardRenderPipeline::applyPendingResourceRefreshes()
{
    bool bRefreshViewportSnapshot   = false;
    bool bRefreshViewportStageState = false;
    bool bRefreshShadowStageState   = false;

    if (hasPendingResourceRefresh(EForwardPendingResourceRefresh::ViewportResize)) {
        _viewportRTSpec.extent = _pendingViewportExtent;
        recreateViewportResources();
        bRefreshViewportSnapshot   = true;
        bRefreshViewportStageState = true;
        clearPendingResourceRefresh(EForwardPendingResourceRefresh::ViewportResize);
    }

    if (hasPendingResourceRefresh(EForwardPendingResourceRefresh::ShadowResources) && _render) {
        _shadowResources.destroy();
        if (currentShadowSettings().isEnabled()) {
            initShadowResources();
        }
        bRefreshShadowStageState = true;
        clearPendingResourceRefresh(EForwardPendingResourceRefresh::ShadowResources);
    }

    if (hasPendingResourceRefresh(EForwardPendingResourceRefresh::AttachmentFormat)) {
        recreateViewportResources();
        bRefreshViewportSnapshot   = true;
        bRefreshViewportStageState = true;
        clearPendingResourceRefresh(EForwardPendingResourceRefresh::AttachmentFormat);
    }

    if (bRefreshViewportSnapshot) {
        refreshViewportSnapshot();
    }
    if (bRefreshViewportStageState) {
        refreshViewportStageState();
    }
    if (bRefreshShadowStageState) {
        refreshShadowStageState();
    }
}

void ForwardRenderPipeline::syncFrameSettings(const RenderPipelineFrameContext& frame)
{
    const auto desiredExtent = Extent2D::fromVec2(frame.viewportRect.extent / frame.viewportFrameBufferScale);
    if (desiredExtent.width > 0 && desiredExtent.height > 0 && !(desiredExtent == _viewportResources.extent)) {
        requestViewportResize(desiredExtent);
    }

    const ShadowSettings shadowSettings          = currentShadowSettings();
    const uint32_t       desiredShadowResolution = std::max(shadowSettings.resolution, 1u);
    if (shadowSettings.isEnabled()) {
        const bool bShadowResolutionDirty = !_shadowResources.depthImage ||
                                            _shadowResources.extent.width != desiredShadowResolution ||
                                            _shadowResources.extent.height != desiredShadowResolution;
        if (bShadowResolutionDirty) {
            requestShadowResourceRefresh();
        }
    }

    syncShadowSettings();
}

void ForwardRenderPipeline::recreateViewportResources()
{
    YA_CORE_ASSERT(_render != nullptr, "ForwardRenderPipeline requires a valid render backend to create viewport resources");
    _viewportResources = buildForwardViewportResources(*_render, _viewportRTSpec);
    YA_CORE_ASSERT(_viewportResources.colorOwner && _viewportResources.depthOwner,
                   "Failed to recreate forward viewport attachment owners");
}

void ForwardRenderPipeline::refreshViewportSnapshot()
{
    _viewportFormats = buildForwardViewportFormats(_viewportRTSpec);
}

void ForwardRenderPipeline::refreshViewportStageState()
{
    if (_viewportStage) {
        _viewportStage->refreshPipelineFormats(_viewportFormats);
    }
}

void ForwardRenderPipeline::refreshShadowStageState()
{
    if (_viewportStage) {
        _viewportStage->setDepthBufferShadowDescriptorSet(depthBufferShadowDS);
    }
    if (currentShadowSettings().isEnabled() && _shadowResources.depthImage) {
        rebuildShadowViews();
    }
    syncShadowSettings();
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

ShadowSettings ForwardRenderPipeline::getCurrentShadowSettings() const
{
    return currentShadowSettings();
}

void ForwardRenderPipeline::requestShadowSettings(const ShadowSettings& shadowSettings)
{
    applyShadowSettings(shadowSettings);
}

void ForwardRenderPipeline::applyShadowSettings(const ShadowSettings& shadowSettings)
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

EFormat::T ForwardRenderPipeline::getViewportColorFormat() const
{
    return !_viewportFormats.colorFormats.empty() ? _viewportFormats.colorFormats.front() : EFormat::Undefined;
}

EFormat::T ForwardRenderPipeline::getViewportDepthFormat() const
{
    return _viewportFormats.depthFormat.value_or(EFormat::Undefined);
}

void ForwardRenderPipeline::executeShadowPass(RenderStageContext& stageCtx)
{
    const ShadowSettings shadowSettings = currentShadowSettings();
    if (!shadowSettings.isEnabled() || !_shadowStage) {
        return;
    }

    _shadowStage->applySettings(shadowSettings);
    _shadowStage->prepare(stageCtx);
}

void ForwardRenderPipeline::executeViewportPass(const RenderPipelineFrameContext& frame, RenderStageContext& stageCtx)
{
    if (_frameResources && !_frameResources->prepareSkinning(stageCtx)) {
        YA_CORE_ERROR("Forward viewport skinning resource prepare failed");
    }

    _viewportStage->prepare(stageCtx);
    if (_frameResources && !_frameResources->prepareFramePayloads(stageCtx, _viewportStage->getFramePayloads())) {
        YA_CORE_ERROR("Forward viewport frame payload upload failed");
    }

    YA_CORE_ASSERT(_viewportResources.colorOwner && _viewportResources.colorImage,
                   "Forward viewport pass requires a color attachment snapshot");
    YA_CORE_ASSERT(_viewportResources.depthOwner && _viewportResources.depthImage,
                   "Forward viewport pass requires a depth attachment snapshot");
    YA_CORE_ASSERT(!_viewportRTSpec.attachments.colorAttach.empty(),
                   "Forward viewport pass requires a color attachment spec");
    YA_CORE_ASSERT(_viewportRTSpec.attachments.depthAttach.has_value(),
                   "Forward viewport pass requires a depth attachment spec");

    _lastTickCtx        = frame.frameData ? frame.frameData->toFrameContext() : FrameContext{
                                                                                  .view       = frame.view,
                                                                                  .projection = frame.projection,
                                                                                  .cameraPos  = frame.cameraPos,
                                                                              };
    _lastTickCtx.extent = _viewportResources.extent;
    _lastFrameInput     = frame;

    [[maybe_unused]] const bool bExecuted = executeViewportPassGraph(frame, stageCtx);
    YA_CORE_ASSERT(bExecuted, "Forward viewport graph execution failed");
}

void ForwardRenderPipeline::finalizeViewportPass(ICommandBuffer* cmdBuf)
{
    auto* inputImage = bMSAA ? _viewportResources.resolveImage : _viewportResources.colorImage;

    if (_postProcessStage.execute(cmdBuf, inputImage, _lastFrameInput.viewportRect.extent, &_lastTickCtx)) {
        _currentPostprocessOutput = _postProcessStage.getPreparedOutputImageShared();
    }
    else {
        _currentPostprocessOutput.reset();
    }

    YA_CORE_ASSERT(inputImage, "Failed to get viewport image for postprocessing");
}

void ForwardRenderPipeline::shutdown()
{
    _getFrameIndex = {};
    _getElapsedTimeSeconds = {};
    getActiveScene = {};
    getResourceResolveSystem = {};
    getSceneSkyboxDescriptorSet = {};
    getSceneEnvironmentLightingDescriptorSet = {};
    _currentPostprocessOutput.reset();
    if (_frameResources) {
        _frameResources->destroy();
        _frameResources.reset();
    }
    _postProcessStage.setGraphExecutor(nullptr);
    _graphExecutor.reset();
    _pendingViewportExtent = {};
    _pendingResourceRefreshMask = 0;
    _viewportFormats = {};
    _viewportResources = {};
    _deleter.clear();
}

bool ForwardRenderPipeline::executeViewportPassGraph(const RenderPipelineFrameContext& frame, RenderStageContext& stageCtx)
{
    YA_CORE_ASSERT(_graphExecutor != nullptr, "ForwardRenderPipeline graph executor is not initialized");

    RenderGraph graph;
    ShadowGraphOutputs shadowOutputs;
    if (_shadowStage && currentShadowSettings().isEnabled()) {
        shadowOutputs = _shadowStage->appendGraphPasses(graph, stageCtx);
    }
    const auto color = graph.importTexture(
        makeForwardViewportImportedDesc(*_viewportResources.colorImage,
                                        "ForwardViewport.Color",
                                        _viewportRTSpec.attachments.colorAttach[0].finalLayout));
    const RGTextureHandle resolve = _viewportResources.resolveImage
        ? graph.importTexture(
              makeForwardViewportImportedDesc(*_viewportResources.resolveImage,
                                              "ForwardViewport.Resolve",
                                              _viewportRTSpec.attachments.colorAttach[0].finalLayout))
        : RGTextureHandle{};
    const auto  depth = graph.importTexture(
        makeForwardViewportImportedDesc(*_viewportResources.depthImage,
                                        "ForwardViewport.Depth",
                                        _viewportRTSpec.attachments.depthAttach->finalLayout));
    const auto shadowDepth = shadowOutputs.shadowDepth;
    const auto viewportExtent = _viewportResources.extent;
    const auto colorAttachment = _viewportRTSpec.attachments.colorAttach[0];
    const auto depthAttachment = *_viewportRTSpec.attachments.depthAttach;
    const auto frameBinding = _frameResources
        ? _frameResources->getBinding(stageCtx.flightIndex)
        : ForwardFrameResourceSet::Binding{};
    const Rect2D renderArea{.pos = {0, 0}, .extent = viewportExtent.toVec2()};

    // FG-702/703: the Forward viewport sequence is declared as separate graph
    // passes (Skybox -> PBR -> Phong -> Unlit -> Rest). The stage exposes one
    // entry per pass; the graph owns the order and the attachment lifetimes.
    // Skybox is the first pass and clears the viewport; Rest is the last pass
    // and owns the MSAA resolve attachment plus the editor viewport overlays.
    // The attachment chain stays in attachment-optimal layout between passes;
    // only Rest applies the final consumer layout
    // (`EImageLayout::ShaderReadOnlyOptimal`, matching the imported final
    // layout used by postprocess outside the graph).
    ForwardSkyboxPassParams skyboxParams{
        .viewportColor = color,
        .viewportDepth = depth,
        .renderArea    = renderArea,
        .layerCount    = 1,
        .finalLayout   = EImageLayout::ColorAttachmentOptimal,
    };
    ForwardPBRPassParams pbrParams{
        .viewportColor = color,
        .viewportDepth = depth,
        .renderArea    = renderArea,
        .layerCount    = 1,
        .finalLayout   = EImageLayout::ColorAttachmentOptimal,
    };
    ForwardPhongPassParams phongParams{
        .viewportColor = color,
        .viewportDepth = depth,
        .renderArea    = renderArea,
        .layerCount    = 1,
        .finalLayout   = EImageLayout::ColorAttachmentOptimal,
    };
    ForwardUnlitPassParams unlitParams{
        .viewportColor = color,
        .viewportDepth = depth,
        .renderArea    = renderArea,
        .layerCount    = 1,
        .finalLayout   = EImageLayout::ColorAttachmentOptimal,
    };
    ForwardRestPassParams restParams{
        .viewportColor = color,
        .viewportDepth = depth,
        .renderArea    = renderArea,
        .layerCount    = 1,
        .finalLayout   = colorAttachment.finalLayout,
        .recordViewportOverlays = _lastFrameInput.recordViewportOverlays
            ? [this](ICommandBuffer* cmdBuf, Extent2D viewportExtent) {
                  YA_PERF_SCOPE(perf::sample::renderViewportOverlay(), perf::metric::cpuTimeMs(), perf::domain::render());
                  _lastFrameInput.recordViewportOverlays(cmdBuf, viewportExtent, _lastTickCtx);
              }
            : std::function<void(ICommandBuffer*, Extent2D)>{},
    };

    [[maybe_unused]] const auto skyboxPass = graph.addPass(
        "Forward Skybox",
        [&skyboxParams, colorAttachment, depthAttachment](RGPassBuilder& passBuilder) {
            passBuilder.declareRaster({
                .renderArea = skyboxParams.renderArea,
                .layerCount = skyboxParams.layerCount,
                .colors = {{
                    .color       = skyboxParams.viewportColor,
                    .clearValue  = ClearValue::Black(),
                    .loadOp      = colorAttachment.loadOp,
                    .storeOp     = colorAttachment.storeOp,
                    .finalLayout = skyboxParams.finalLayout,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = skyboxParams.viewportDepth,
                    .clearValue  = ClearValue(1.0f, 0),
                    .loadOp      = depthAttachment.loadOp,
                    .storeOp     = depthAttachment.storeOp,
                    .finalLayout = depthAttachment.finalLayout,
                },
            });
        },
        [this, &stageCtx, frameBinding](RGRenderContext& rgCtx) {
            const auto rasterParams   = rgCtx.getRasterPassExecutionParams();
            const auto viewportExtent = rasterParams.getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();

            stageCtx.viewportExtent = viewportExtent;
            _viewportStage->executeSkybox(stageCtx, frameBinding);
            rgCtx.endRendering();
        });

    [[maybe_unused]] const auto pbrPass = graph.addPass(
        "Forward PBR",
        [&pbrParams, shadowDepth, shadowOutputs, depthAttachment](RGPassBuilder& passBuilder) {
            if (shadowOutputs.lastPass.has_value()) {
                passBuilder.dependsOn(*shadowOutputs.lastPass);
            }
            if (shadowDepth.has_value()) {
                passBuilder.read(*shadowDepth);
            }
            passBuilder.declareRaster({
                .renderArea = pbrParams.renderArea,
                .layerCount = pbrParams.layerCount,
                .colors = {{
                    .color       = pbrParams.viewportColor,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = pbrParams.finalLayout,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = pbrParams.viewportDepth,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = depthAttachment.finalLayout,
                },
            });
        },
        [this, &stageCtx, frameBinding](RGRenderContext& rgCtx) {
            const auto rasterParams   = rgCtx.getRasterPassExecutionParams();
            const auto viewportExtent = rasterParams.getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();

            stageCtx.viewportExtent = viewportExtent;
            _viewportStage->executePBR(stageCtx, frameBinding);
            rgCtx.endRendering();
        });

    [[maybe_unused]] const auto phongPass = graph.addPass(
        "Forward Phong",
        [&phongParams, shadowDepth, shadowOutputs, depthAttachment](RGPassBuilder& passBuilder) {
            if (shadowOutputs.lastPass.has_value()) {
                passBuilder.dependsOn(*shadowOutputs.lastPass);
            }
            if (shadowDepth.has_value()) {
                passBuilder.read(*shadowDepth);
            }
            passBuilder.declareRaster({
                .renderArea = phongParams.renderArea,
                .layerCount = phongParams.layerCount,
                .colors = {{
                    .color       = phongParams.viewportColor,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = phongParams.finalLayout,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = phongParams.viewportDepth,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = depthAttachment.finalLayout,
                },
            });
        },
        [this, &stageCtx, frameBinding](RGRenderContext& rgCtx) {
            const auto rasterParams   = rgCtx.getRasterPassExecutionParams();
            const auto viewportExtent = rasterParams.getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();

            stageCtx.viewportExtent = viewportExtent;
            _viewportStage->executePhong(stageCtx, frameBinding);
            rgCtx.endRendering();
        });

    [[maybe_unused]] const auto unlitPass = graph.addPass(
        "Forward Unlit",
        [&unlitParams, depthAttachment](RGPassBuilder& passBuilder) {
            passBuilder.declareRaster({
                .renderArea = unlitParams.renderArea,
                .layerCount = unlitParams.layerCount,
                .colors = {{
                    .color       = unlitParams.viewportColor,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = unlitParams.finalLayout,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = unlitParams.viewportDepth,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = depthAttachment.finalLayout,
                },
            });
        },
        [this, &stageCtx, frameBinding](RGRenderContext& rgCtx) {
            const auto rasterParams   = rgCtx.getRasterPassExecutionParams();
            const auto viewportExtent = rasterParams.getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();

            stageCtx.viewportExtent = viewportExtent;
            _viewportStage->executeUnlit(stageCtx, frameBinding);
            rgCtx.endRendering();
        });

    [[maybe_unused]] const auto restPass = graph.addPass(
        "Forward Rest",
        [&restParams, resolve, depthAttachment](RGPassBuilder& passBuilder) {
            passBuilder.declareRaster({
                .renderArea = restParams.renderArea,
                .layerCount = restParams.layerCount,
                .colors = {{
                    .color       = restParams.viewportColor,
                    .resolve     = resolve,
                    .resolveMode = resolve.isValid() ? EResolveMode::Average : EResolveMode::None,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = restParams.finalLayout,
                }},
                .depth = RGDepthAttachmentDesc{
                    .depth       = restParams.viewportDepth,
                    .loadOp      = EAttachmentLoadOp::Load,
                    .storeOp     = EAttachmentStoreOp::Store,
                    .finalLayout = depthAttachment.finalLayout,
                },
            });
        },
        [this, &stageCtx, restParams](RGRenderContext& rgCtx) {
            const auto rasterParams   = rgCtx.getRasterPassExecutionParams();
            const auto viewportExtent = rasterParams.getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();

            stageCtx.viewportExtent = viewportExtent;
            _viewportStage->executeRest(stageCtx);
            if (restParams.recordViewportOverlays) {
                restParams.recordViewportOverlays(&rgCtx.getCommandBuffer(), viewportExtent);
            }
            rgCtx.endRendering();
        });

    return _graphExecutor->execute(graph, *frame.cmdBuf);
}

void ForwardRenderPipeline::onViewportResized(Rect2D rect)
{
    Extent2D newExtent{
        .width  = static_cast<uint32_t>(rect.extent.x),
        .height = static_cast<uint32_t>(rect.extent.y),
    };
    requestViewportResize(newExtent);
}

Extent2D ForwardRenderPipeline::getViewportExtent() const
{
    return _viewportResources.extent;
}

IImageView* ForwardRenderPipeline::getShadowPointFaceDepthIV(uint32_t pointLightIndex, uint32_t faceIndex) const
{
    if (pointLightIndex >= MAX_POINT_LIGHTS || faceIndex >= 6) return nullptr;
    return _shadowResources.pointFaceIVs[pointLightIndex][faceIndex].get();
}

} // namespace ya
