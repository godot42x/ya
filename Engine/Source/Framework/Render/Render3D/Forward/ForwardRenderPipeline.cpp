#include "Render3D/Forward/ForwardRenderPipeline.h"

#include "RHI/Backend/Vulkan/VulkanRender.h"
#include "RHI/Core/Buffer.h"
#include "Core/Profiling/Profiling.h"
#include "RHI/Core/RenderingInfoUtils.h"
#include "RHI/Core/Sampler.h"
#include "ECS/Systems/Components/DirectionComponent.h"
#include "Scene3D/TransformComponent.h"
#include "Render3D/Forward/ForwardFrameGraphOrchestrator.h"
#include "Scene/Core/Scene.h"
#include <glm/gtc/matrix_transform.hpp>

namespace ya
{

namespace
{

ForwardDirectionGizmoInput buildForwardDirectionGizmoInput(const TransformComponent& tc)
{
    const glm::mat4 worldTransform = glm::translate(glm::mat4(1.0f), tc.getWorldPosition()) *
                                     glm::mat4_cast(glm::quat(glm::radians(tc.getRotation())));
    const glm::mat4 coneLocalTransf =
        glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1, 0, 0)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(0.3f, 1.0f, 0.3f));
    const glm::mat4 cylinderLocalTransf =
        glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1, 0, 0)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(0.1f, 1.0f, 0.1f));

    return ForwardDirectionGizmoInput{
        .coneModel     = glm::translate(glm::mat4(1.0f), -tc.getForward()) * coneLocalTransf * worldTransform,
        .cylinderModel = worldTransform * cylinderLocalTransf,
        .lineStart     = tc.getWorldPosition(),
        .lineEnd       = tc.getWorldPosition() + tc.getForward(),
    };
}

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
    _runtimeServices        = desc.runtimeServices;
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
    _entityIdPass.init(_render, EFormat::R32_UINT, DEPTH_FORMAT);
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
        .runtimeServices                    = _runtimeServices,
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
    _viewportResources.reset(_viewportRTSpec.extent);
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

void ForwardRenderPipeline::shutdown()
{
    _entityIdPass.destroy();
    _runtimeServices = nullptr;
    _currentPostprocessOutput.reset();
    if (_frameResources) {
        _frameResources->destroy();
        _frameResources.reset();
    }
    _graphExecutor.reset();
    _pendingViewportExtent = {};
    _pendingResourceRefreshMask = 0;
    _viewportFormats = {};
    _viewportResources.reset();
    _deleter.clear();
}

bool ForwardRenderPipeline::executeViewportPassGraph(const RenderPipelineFrameContext& frame, RenderStageContext& stageCtx)
{
    YA_CORE_ASSERT(_graphExecutor != nullptr, "ForwardRenderPipeline graph executor is not initialized");

    // FG-704: build the direction gizmo snapshot before graph construction so
    // passes never query the ECS during execute.
    std::vector<ForwardDirectionGizmoInput> directionGizmos;
    if (auto* activeScene = _runtimeServices ? _runtimeServices->getActiveScene() : nullptr) {
        const auto& dirView = activeScene->getRegistry().view<TransformComponent, DirectionComponent>();
        for (auto entity : dirView) {
            const auto& [tc, direction] = dirView.get(entity);
            (void)direction;
            directionGizmos.push_back(buildForwardDirectionGizmoInput(tc));
        }
    }

    RenderGraph graph;
    auto viewportPassContext = _viewportStage->buildPassContext(stageCtx);
    _frameGraphOrchestrator.build(
        ForwardFrameGraphOrchestrator::BuildDependencies{
            .viewportStage    = _viewportStage.get(),
            .entityIdPass     = &_entityIdPass,
            .shadowStage      = _shadowStage.get(),
            .postProcessStage = &_postProcessStage,
        },
        ForwardFrameGraphOrchestrator::BuildInputs{
            .graph                    = &graph,
            .stageCtx                 = &stageCtx,
            .frameBinding             = _frameResources ? _frameResources->getBinding(stageCtx.flightIndex)
                                                        : ForwardFrameResourceSet::Binding{},
            .viewportRTSpec           = &_viewportRTSpec,
            .directionGizmos          = std::move(directionGizmos),
            .viewportPassContext      = &viewportPassContext,
            .postContext              = &_lastTickCtx,
            .bEnableShadow            = _shadowStage && currentShadowSettings().isEnabled(),
            .bPostprocessOutputIsSRGB = EFormat::isSRGB(_render->getSwapchain()->getFormat()),
            .viewportOverlaySnapshot  = _lastFrameInput.viewportOverlaySnapshot,
        });

    RGCompiledGraph compiled{};
    RenderGraphExecutionResult result;
    const bool bExecuted = _graphExecutor->execute(graph, *frame.cmdBuf, &compiled, &result);
    if (bExecuted) {
        _lastFrameGraphTopology = graph.describeCompiledTopology(compiled);
        _viewportResources.publish(
            result.getExportedTextureShared(forward_graph_exports::viewportColor),
            result.getExportedTextureShared(forward_graph_exports::viewportDepth),
            result.getExportedTextureShared(forward_graph_exports::viewportResolve),
            result.getExportedTextureShared(forward_graph_exports::entityId),
            _viewportRTSpec.extent);
        _currentPostprocessOutput = result.getExportedTextureShared(PostProcessingStage::kOutputExportName);
    }
    else {
        _lastFrameGraphTopology = {};
    }
    return bExecuted;
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
