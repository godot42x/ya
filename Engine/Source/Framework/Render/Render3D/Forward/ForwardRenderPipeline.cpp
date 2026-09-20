#include "Render3D/Forward/ForwardRenderPipeline.h"

#include "RHI/Backend/Vulkan/VulkanRender.h"
#include "RHI/Core/Buffer.h"
#include "Core/Profiling/Profiling.h"
#include "RHI/Core/RenderingInfoUtils.h"
#include "RHI/Core/Sampler.h"
#include "ECS/Systems/Components/DirectionComponent.h"
#include "Scene3D/TransformComponent.h"
#include "Render3D/Common/PipelineCommon.h"
#include "Render3D/Common/PostProcessingStateConfig.h"
#include "Render3D/Common/RenderSubmission.h"
#include "Render3D/Common/ViewPassResources.h"
#include "Render3D/Common/ViewPersistentResourceKey.h"
#include "Render3D/Common/SceneRenderScheduler.h"
#include "Render3D/Pipelines/BloomPostprocessing.h"
#include "Graph/RenderGraph.h"
#include "Render3D/Forward/ForwardFrameGraphOrchestrator.h"
#include "Scene/Core/Scene.h"
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <vector>

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

RenderAttachmentFormats buildForwardViewFormats(const RenderTargetCreateInfo& spec)
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

RenderTargetCreateInfo buildForwardViewRenderTargetSpec(Extent2D extent, EFormat::T colorFormat, EFormat::T depthFormat)
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

void allocateForwardViewPassResources(
    RenderSubmission&                        submission,
    IRender*                                 render,
    uint32_t                                 alignment,
    const RenderStageContext&                stageCtx,
    EntityIdPass*                    entityIdPass,
    ForwardViewAuxPasses&                auxPasses,
    PostProcessingStage*                     postStage,
    ForwardFrameResourceSet::ViewResources&  resources)
{
    const RenderFrameData* frameData = stageCtx.frameData;
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

    if (auxPasses.getDebugDSL() && frameData) {
        auto& debugUBO = auxPasses.getDebugUBO();
        debugUBO.projection = frameData->projection;
        debugUBO.view       = frameData->view;
        debugUBO.resolution = glm::ivec2(
            static_cast<int>(stageCtx.viewportExtent.width),
            static_cast<int>(stageCtx.viewportExtent.height));
        writeUniformPassBinding(
            submission,
            render,
            auxPasses.getDebugDSL(),
            alignment,
            &debugUBO,
            sizeof(debugUBO),
            resources.debug.ubo);
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
    _viewportRTSpec = buildForwardViewRenderTargetSpec(
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
    _postProcessStage.getState() = postprocess_settings::loadRuntimeSettings(_postProcessStage.getState());
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
    _viewportStage = ya::makeShared<ForwardViewStage>();
    _viewportStage->initWithDesc(ForwardViewStage::InitDesc{
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
    });

    _deleter.push("Stages", [this](void*)
                  {
        if (_viewportStage) { _viewportStage->destroy(); _viewportStage.reset(); }
        if (_shadowStage) { _shadowStage->destroy(); _shadowStage.reset(); } });
}

ViewFamilyRenderResult ForwardRenderPipeline::recordFamily(const ViewFamilyRecordContext& ctx)
{
    YA_PROFILE_FUNCTION();

    ViewFamilyRenderResult result;
    if (ctx.family) {
        result.key = ctx.family->key;
    }
    if (!ctx.cmdBuf || !ctx.submission || !ctx.submission->isRecording()) {
        return result;
    }

    applyPendingPostProcessSettings();
    _postProcessStage.beginFrame();

    // A family exists because a Scene has content and a View declared it, so an
    // empty family is not a tick to synthesize a View for: there is no View id,
    // no output identity and no declared geometry to record into.
    std::vector<SceneViewRecording> recordings = ctx.views;
    if (recordings.empty()) {
        return result;
    }

    struct ForwardFamilyViewBranch
    {
        RenderPipelineFrameContext frame{};
        RenderStageContext         stageCtx{};
        /// The View's own shadow preparation result, carried from prepare to the
        /// graph build so the shadow stage needs no "current View".
        ShadowPreparedView         shadowPrepared{};
        FrameContext               postContext{};
        std::unique_ptr<ForwardViewStage::PassContext> viewportPassContext;
        ForwardFrameResourceSet::Binding frameBinding{};
        ForwardFrameResourceSet::ViewResources* viewResources = nullptr;
    };

    RenderGraph graph;
    std::vector<ForwardFamilyViewBranch> liveBranches;
    liveBranches.reserve(recordings.size());
    std::optional<RGPassHandle> familyPredecessor;
    bool preparedSkinning = false;

    for (const SceneViewRecording& recording : recordings) {
        ForwardFamilyViewBranch branch;
        // The View's own declaration and prepared data are the camera: there is
        // no host packet to copy and override field by field.
        const Extent2D viewExtent = recording.task ? recording.task->output.extent : Extent2D{};
        branch.frame = RenderPipelineFrameContext{
            .cmdBuf                  = ctx.cmdBuf,
            .frame                   = ctx.frame,
            .submission              = ctx.submission,
            .view                    = RenderViewRecordingContext{
                .task           = recording.task,
                .frameData      = recording.frameData,
                .viewportExtent = viewExtent,
            },
            .derivedScene            = recording.task ? recording.task->desc.scene : nullptr,
        };
        if (shouldSkipView(branch.frame)) {
            continue;
        }

        captureShadowSettings(branch.frame);
        syncFrameSettings(branch.frame);
        applyPendingResourceRefreshes();
        beginViewRecording(branch.frame, branch.stageCtx);
        branch.shadowPrepared = executeShadowPass(branch.frame, branch.stageCtx);

        RenderViewRecordingContext view = branch.frame.view;
        if (view.viewportExtent.width == 0 && view.viewportExtent.height == 0) {
            view.viewportExtent = branch.stageCtx.viewportExtent;
        }
        if (_frameResources && !preparedSkinning) {
            if (!_frameResources->prepareSkinning(*branch.frame.submission, view)) {
                YA_CORE_ERROR("Forward viewport skinning resource prepare failed");
            }
            preparedSkinning = true;
        }

        _viewportStage->prepare(branch.stageCtx);
        if (_frameResources) {
            const auto* binding = _frameResources->beginView(
                *branch.frame.submission, view, _viewportStage->getFramePayloads());
            if (!binding) {
                YA_CORE_ERROR("Forward viewport view binding prepare failed");
                continue;
            }
            branch.frameBinding = *binding;
            branch.viewResources = _frameResources->mutableViewResources(
                branch.frame.submission->flightIndex(), view.viewSlot);
            if (branch.viewResources) {
                const uint32_t alignment = std::max(_render ? _render->getUniformBufferOffsetAlignment() : 1u, 1u);
                allocateForwardViewPassResources(
                    *branch.frame.submission,
                    _render,
                    alignment,
                    branch.stageCtx,
                    &_entityIdPass,
                    _viewportStage->getAuxPasses(),
                    &_postProcessStage,
                    *branch.viewResources);
            }
        }

        branch.postContext = {};
        if (const RenderFrameData* frameData = branch.frame.view.frameData) {
            branch.postContext.view                 = frameData->view;
            branch.postContext.projection           = frameData->projection;
            branch.postContext.viewProjection       = frameData->viewProjection;
            branch.postContext.cameraPos            = frameData->cameraPos;
            branch.postContext.bHasDirectionalLight = frameData->sceneSnapshot && frameData->sceneSnapshot->bHasDirectionalLight;
            branch.postContext.directionalLight     = frameData->directionalLight;
            branch.postContext.numPointLights       = frameData->numPointLights;
            branch.postContext.pointLights          = frameData->pointLights;
            branch.postContext.viewOwner            = frameData->viewOwner;
            branch.postContext.extent               = frameData->viewportExtent;
        }
        branch.postContext.extent         = branch.stageCtx.viewportExtent;

        liveBranches.push_back(std::move(branch));
        ForwardFamilyViewBranch& live = liveBranches.back();
        live.viewportPassContext = std::make_unique<ForwardViewStage::PassContext>(
            _viewportStage->buildPassContext(live.stageCtx));
        if (live.viewResources) {
            live.viewportPassContext->debug = live.viewResources->debug;
        }
        if (!appendViewportPassGraph(
                graph,
                live.frame,
                live.stageCtx,
                live.shadowPrepared,
                live.postContext,
                *live.viewportPassContext,
                live.frameBinding,
                live.viewResources,
                familyPredecessor)) {
            liveBranches.pop_back();
            continue;
        }
        familyPredecessor = graph.lastPassHandle();
    }

    if (liveBranches.empty()) {
        return result;
    }

    YA_CORE_ASSERT(_graphExecutor != nullptr, "ForwardRenderPipeline graph executor is not initialized");
    RGCompiledGraph compiled{};
    RenderGraphExecutionResult execution;
    const bool bExecuted = _graphExecutor->execute(graph, *ctx.cmdBuf, &compiled, &execution);
    if (!bExecuted) {
        _lastFrameGraphTopology = {};
        return result;
    }
    _lastFrameGraphTopology = graph.describeCompiledTopology(compiled);

    for (const ForwardFamilyViewBranch& branch : liveBranches) {
        const uint64_t viewId = branch.frame.view.task ? branch.frame.view.task->desc.viewId : 0;
        RenderViewOutput output = collectViewOutput(
            execution, branch.frame.view.task, viewId, branch.stageCtx.viewportExtent);
        const bool bDisplayRoot = branch.frame.view.task && ctx.plan &&
                                  branch.frame.view.task == ctx.plan->displayRootTask();
        if (bDisplayRoot) {
            _viewportResources.publish(output.color, output.depth, nullptr, output.entityId, branch.stageCtx.viewportExtent);
        }
        result.views.push_back(std::move(output));
    }
    return result;
}

bool ForwardRenderPipeline::shouldSkipView(const RenderPipelineFrameContext& frame) const
{
    YA_CORE_ASSERT(frame.cmdBuf, "ForwardRenderPipeline requires command buffer");
    // A View with no extent has no offscreen output to record into.
    return frame.view.viewportExtent.width == 0 || frame.view.viewportExtent.height == 0;
}

void ForwardRenderPipeline::beginViewRecording(const RenderPipelineFrameContext& frame, RenderStageContext& stageCtx)
{
    Extent2D viewExtent = frame.view.viewportExtent;
    if (viewExtent.width == 0 || viewExtent.height == 0) {
        viewExtent = _viewportResources.extent;
    }

    stageCtx = RenderStageContext{
        .cmdBuf         = frame.cmdBuf,
        .frameData      = frame.view.frameData,
        .flightIndex    = frame.frame ? frame.frame->flightIndex : 0,
        .frameIndex     = frame.frame ? frame.frame->frameIndex : 0,
        .deltaTime      = frame.frame ? frame.frame->deltaTime : 0.0f,
        .viewportExtent = viewExtent,
        .derivedScene   = frame.derivedScene,
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
    if (sceneViewOwnsHostViewport(frame.view.task)) {
        const float frameBufferScale = std::max(frame.frame ? frame.frame->viewportFrameBufferScale : 1.0f, 1.0f);
        const auto  desiredExtent    = Extent2D::fromVec2(glm::vec2{static_cast<float>(frame.view.viewportExtent.width),
                                                                    static_cast<float>(frame.view.viewportExtent.height)} / frameBufferScale);
        if (desiredExtent.width > 0 && desiredExtent.height > 0 && !(desiredExtent == _viewportResources.extent)) {
            requestViewportResize(desiredExtent);
        }
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
    _viewportFormats = buildForwardViewFormats(_viewportRTSpec);
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
    if (frame.frame && frame.frame->shadowSettings) {
        _frameShadowSettings = *frame.frame->shadowSettings;
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

PostProcessingState ForwardRenderPipeline::getPostProcessSettings() const
{
    return _postProcessStage.getState();
}

PostProcessingState ForwardRenderPipeline::resolvePostProcessSettings() const
{
    return _pendingPostProcessSettings ? *_pendingPostProcessSettings : _postProcessStage.getState();
}

void ForwardRenderPipeline::requestPostProcessSettings(const PostProcessingState& settings)
{
    _pendingPostProcessSettings = settings;
}

void ForwardRenderPipeline::applyPendingPostProcessSettings()
{
    if (!_pendingPostProcessSettings) {
        return;
    }
    _postProcessStage.getState() = *_pendingPostProcessSettings;
    _pendingPostProcessSettings.reset();
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

ShadowPreparedView ForwardRenderPipeline::executeShadowPass(const RenderPipelineFrameContext& frame,
                                                            RenderStageContext&               stageCtx)
{
    const ShadowSettings shadowSettings = currentShadowSettings();
    if (!shadowSettings.isEnabled() || !_shadowStage) {
        return {};
    }

    _shadowStage->applySettings(shadowSettings);

    if (!frame.submission || !frame.submission->isRecording()) {
        YA_CORE_ERROR("Forward shadow pass requires a recording submission");
        return {};
    }

    RenderViewRecordingContext view = frame.view;
    if (view.viewportExtent.width == 0 && view.viewportExtent.height == 0) {
        view.viewportExtent = stageCtx.viewportExtent;
    }
    return _shadowStage->prepareView(*frame.submission, view);
}

void ForwardRenderPipeline::shutdown()
{
    _entityIdPass.destroy();
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

bool ForwardRenderPipeline::appendViewportPassGraph(RenderGraph& graph,
                                                    const RenderPipelineFrameContext& frame,
                                                    RenderStageContext&             stageCtx,
                                                    const ShadowPreparedView&       shadowPrepared,
                                                    FrameContext&                    postContext,
                                                    ForwardViewStage::PassContext& viewportPassContext,
                                                    const ForwardFrameResourceSet::Binding& frameBinding,
                                                    ForwardFrameResourceSet::ViewResources* viewResources,
                                                    std::optional<RGPassHandle> familyPredecessor)
{
    YA_CORE_ASSERT(_graphExecutor != nullptr, "ForwardRenderPipeline graph executor is not initialized");

    std::vector<ForwardDirectionGizmoInput> directionGizmos;
    if (auto* activeScene = frame.derivedScene) {
        const auto& dirView = activeScene->getRegistry().view<TransformComponent, DirectionComponent>();
        for (auto entity : dirView) {
            const auto& [tc, direction] = dirView.get(entity);
            (void)direction;
            directionGizmos.push_back(buildForwardDirectionGizmoInput(tc));
        }
    }

    RenderTargetCreateInfo viewRTSpec = _viewportRTSpec;
    if (stageCtx.viewportExtent.width > 0 && stageCtx.viewportExtent.height > 0) {
        viewRTSpec.extent = stageCtx.viewportExtent;
    }
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
            .frameBinding             = frameBinding,
            .viewportRTSpec           = &viewRTSpec,
            .directionGizmos          = std::move(directionGizmos),
            .viewportPassContext      = &viewportPassContext,
            .postContext              = &postContext,
            .bEnableShadow            = _shadowStage && currentShadowSettings().isEnabled(),
            .shadowPrepared           = shadowPrepared,
            .bPostprocessOutputIsSRGB = EFormat::isSRGB(POSTPROCESS_COLOR_FORMAT),
            .viewId                   = frame.view.task ? frame.view.task->desc.viewId : 0,
            .viewResources            = viewResources,
            .familyPredecessor        = familyPredecessor,
        });
    return true;
}

RenderViewOutput ForwardRenderPipeline::collectViewOutput(const RenderGraphExecutionResult& result,
                                                          const SceneViewportTask* task,
                                                          uint64_t viewId,
                                                          Extent2D viewExtent) const
{
    RenderViewOutput output;
    if (task) {
        output.desc = task->output;
    }
    output.desc.viewId = viewId != 0 ? viewId : (task ? task->desc.viewId : 0);
    if (!output.desc.hasExtent()) {
        output.desc.extent = viewExtent;
    }
    output.color    = result.getExportedTextureShared(makeViewGraphName(forward_graph_exports::viewportColor, viewId));
    output.depth    = result.getExportedTextureShared(makeViewGraphName(forward_graph_exports::viewportDepth, viewId));
    output.entityId = result.getExportedTextureShared(makeViewGraphName(forward_graph_exports::entityId, viewId));
    output.bloomExtract = result.getExportedTextureShared(makeViewGraphName(BloomPostprocessing::kExtractExportName, viewId));
    output.bloomBlur    = result.getExportedTextureShared(makeViewGraphName(BloomPostprocessing::kBlurPongExportName, viewId));
    if (!output.bloomBlur) {
        output.bloomBlur = result.getExportedTextureShared(makeViewGraphName(BloomPostprocessing::kBlurPingExportName, viewId));
    }
    output.bloomComposite = result.getExportedTextureShared(makeViewGraphName(BloomPostprocessing::kOutputExportName, viewId));
    const auto postprocess = result.getExportedTextureShared(makeViewGraphName(PostProcessingStage::kOutputExportName, viewId));
    output.display = postprocess ? postprocess : output.color;
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

std::shared_ptr<ImageResource> ForwardRenderPipeline::getShadowDirectionalDepthResource() const
{
    return makeShadowDebugResource(
        _shadowResources.depthImage,
        _shadowResources.directionalDepthIV,
        "Forward.ShadowDirectionalDepth");
}

std::shared_ptr<ImageResource> ForwardRenderPipeline::getShadowPointFaceDepthResource(uint32_t pointLightIndex, uint32_t faceIndex) const
{
    if (pointLightIndex >= MAX_POINT_LIGHTS || faceIndex >= 6) return nullptr;
    return makeShadowDebugResource(
        _shadowResources.depthImage,
        _shadowResources.pointFaceIVs[pointLightIndex][faceIndex],
        std::format("Forward.ShadowPointDepth.{}.{}", pointLightIndex, faceIndex));
}

} // namespace ya
