#include "Render3D/RenderDeviceState.h"

#include "Core/Log.h"
#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Core/Profiling/Profiling.h"
#include "RHI/Core/CommandBuffer.h"
#include <vector>

namespace ya
{

bool RenderDeviceState::beginFrameCommandBuffer(const RenderFramePlan& plan, std::shared_ptr<ICommandBuffer>& cmdBuf)
{
    YA_PROFILE_SCOPE("RenderDeviceState::beginFrameCommandBuffer");
    YA_PERF_SCOPE(perf::sample::renderPrepareFrame(), perf::metric::cpuTimeMs(), perf::domain::render());

    if (!plan.present.surface || plan.present.imageIndex < 0) {
        // Host skipped acquire (unpresentable / failed begin). One cmdBuf still
        // couples world record to present, so there is nothing to record here.
        // Camera skip is host policy, not a swapchain query inside the coordinator.
        return false;
    }

    const uint32_t flightIndex = plan.frame.flightIndex;
    if (flightIndex >= _commandBuffers.size() || !_commandBuffers[flightIndex]) {
        YA_CORE_ERROR("Recording flight {} has no command buffer", flightIndex);
        return false;
    }

    cmdBuf = _commandBuffers[flightIndex];
    cmdBuf->reset();
    cmdBuf->begin();
    if (YA_PERF_IS_ENABLED()) {
        _render->beginFrameGpuTiming(cmdBuf.get());
    }

    if (!_submissions.acquire(flightIndex, plan.frame.frameIndex, cmdBuf.get(), plan.present.surface)) {
        YA_CORE_ERROR("Recording flight {} failed to begin a live submission", flightIndex);
        return false;
    }
    if (!_viewOutputs.beginSubmission(flightIndex, plan.frame.frameIndex)) {
        YA_CORE_ERROR("Recording flight {} failed to begin view outputs", flightIndex);
        return false;
    }
    return true;
}

void RenderDeviceState::recordViewFamilies(const RenderFramePlan& plan)
{
    YA_PROFILE_FUNCTION();

    ISceneViewFamilyRenderer* pipeline = getActivePipeline();
    YA_CORE_ASSERT(pipeline, "Active render pipeline is null while recording a view family");

    RenderSubmission* live = _submissions.get(plan.frame.flightIndex);
    YA_CORE_ASSERT(live && live->isRecording(), "Family record requires a recording submission");

    const auto recordOneFamily = [&](const SceneViewFamilyPlan* family, std::vector<SceneViewRecording> views) {
        ViewFamilyRecordContext ctx{
            .cmdBuf     = live->commandBuffer(),
            .frame      = &plan.frame,
            .submission = live,
            .plan       = &plan.sceneRender.plan(),
            .family     = family,
            .views      = std::move(views),
        };
        publishFamilyResult(plan.frame.flightIndex, pipeline->recordFamily(ctx));
    };

    // The plan arrives extracted, so its views hold one recording per view
    // task and the family indices always land inside them.
    const std::vector<SceneViewRecording>& views = plan.sceneRender.views();
    for (const SceneViewFamilyPlan& family : plan.sceneRender.plan().viewFamilies) {
        std::vector<SceneViewRecording> familyViews;
        familyViews.reserve(family.viewTaskIndices.size());
        for (uint32_t index : family.viewTaskIndices) {
            familyViews.push_back(views[index]);
        }
        recordOneFamily(&family, std::move(familyViews));
    }
}

void RenderDeviceState::prepareFrameRecord(const RenderFramePlan& plan)
{
    const std::vector<Scene*> scenes = renderedScenes(plan.sceneRender.plan());
    prepareDerivedState(scenes, plan.frame.deltaTime);
    // One call for this frame's safe-point mutations: pending pipeline
    // switch/reload and the queued render-target format commands. A View's
    // geometry is not pushed here -- it is that View's own declaration and its
    // resources are keyed by it. See PipelineCoordinator::applyPendingChanges.
    _pipelineCoordinator.applyPendingChanges();
    // Prepares the runtime UI-compose pass from the pipeline's own postprocess
    // format. There used to be a second, conditional call right below that read
    // the *previous* frame's published display image to learn the same format --
    // a stale read whose only answer this one already has.
    prepareComposePipelines();
    // Pre-record preparation: resolve each View's Scene-keyed GPU bindings now,
    // while the View's own declaration still names its Scene, so recording never
    // has to ask which Scene is current. Skipping this would leave every pass
    // with empty IBL/skybox bindings rather than an obviously wrong one.
    for (const SceneViewRecording& recording : plan.sceneRender.views()) {
        if (recording.frameData) {
            resolveViewSceneResources(recording.task ? recording.task->desc.scene : nullptr,
                                      recording.frameData->sceneResources);
        }
    }
}

void RenderDeviceState::resolveViewSceneResources(Scene* scene, RenderViewSceneResources& out)
{
    // Same calls the passes used to make through the services interface, moved
    // to the point where the View's Scene is known and recording has not begun.
    // Nothing here reads "the current Scene": the caller names the Scene.
    out.clear();
    out.environmentLighting            = _environmentLightingProcessor.get();
    out.environmentLightingResources   = _sharedResourceProvider.resolveSceneEnvironmentLightingResources(scene);
    out.skyboxDescriptorSet            = _sharedResourceProvider.getSceneSkyboxDescriptorSet(scene);
    out.environmentLightingDescriptorSet =
        _sharedResourceProvider.getSceneEnvironmentLightingDescriptorSet(scene);
}

FSurfaceImage RenderDeviceState::surfaceImageFor(const RenderViewOutput* output) const
{
    if (!output) {
        return {};
    }
    auto image = output->displayImage();
    if (!image) {
        return {};
    }

    // `displayImage()` falls back to the raw colour attachment when the finalize
    // output is absent, and that image is the renderer's linear colour rather
    // than a display image. Naming it as linear is the difference between the
    // surface reporting the mismatch and the window quietly going dark.
    if (!output->display) {
        return FSurfaceImage{.image = std::move(image), .encoding = EImageEncoding::Linear};
    }

    EImageEncoding encoding = EImageEncoding::DisplayEncoded;
    if (auto* pipeline = _pipelineCoordinator.getActivePipeline()) {
        encoding = pipeline->getDisplayImageEncoding();
    }
    return FSurfaceImage{.image = std::move(image), .encoding = encoding};
}

const RenderViewOutput* RenderDeviceState::getViewOutput(uint32_t flightIndex, SceneViewId viewId) const
{
    // The flight is the caller's answer, not this object's memory. Scanning
    // other flights would answer with a View from another frame that happens to
    // share an id -- which is exactly how a stale image gets shown as if it were
    // current.
    return _viewOutputs.find(flightIndex, viewId);
}

void RenderDeviceState::publishFamilyResult(uint32_t flightIndex, ViewFamilyRenderResult familyResult)
{
    for (RenderViewOutput& output : familyResult.views) {
        const uint64_t viewId = output.desc.viewId;
        if (viewId == 0) {
            continue;
        }
        if (!_viewOutputs.publish(flightIndex, std::move(output))) {
            YA_CORE_ERROR("Failed to publish view output for view {}", viewId);
        }
    }
}

void RenderDeviceState::retainPublishedViewOutputs(uint32_t flightIndex, ICommandBuffer* cmdBuf)
{
    auto retain = [&](auto resource) {
        if (!resource) {
            return;
        }
        if (RenderSubmission* submission = _submissions.get(flightIndex)) {
            submission->retain(resource);
        }
        cmdBuf->retireResource(resource);
    };

    const uint32_t liveCount = _viewOutputs.liveViewCount(flightIndex);
    for (uint32_t slot = 0; slot < liveCount; ++slot) {
        const RenderViewOutput* output = _viewOutputs.get(flightIndex, slot);
        if (!output) {
            continue;
        }
        retain(output->displayImage());
        retain(output->color);
        retain(output->depth);
        retain(output->entityId);
        retain(output->ssao);
        retain(output->bloomExtract);
        retain(output->bloomBlur);
        retain(output->bloomComposite);
        for (const auto& gBufferColor : output->gBufferColors) {
            retain(gBufferColor);
        }
    }
}

void RenderDeviceState::endFrameCommandBuffer(ICommandBuffer* cmdBuf)
{
    YA_PROFILE_FUNCTION();

    if (YA_PERF_IS_ENABLED()) {
        YA_PROFILE_SCOPE("RenderDeviceState::endGpuTiming");
        _render->endFrameGpuTiming(cmdBuf);
    }

    {
        YA_PROFILE_SCOPE("RenderDeviceState::endCommandBuffer");
        cmdBuf->end();
    }

    if (YA_PERF_IS_ENABLED()) {
        YA_PROFILE_SCOPE("RenderDeviceState::publishGpuMetrics");
        PerfState::get().setValue(
            perf::sample::hostTick(),
            perf::metric::gpuTimeMs(),
            _render->getLastCompletedFrameGpuTimeMs(),
            perf::domain::gpu());
    }
}

RecordedFrame RenderDeviceState::sealFrame(uint32_t flightIndex, ICommandBuffer* cmdBuf)
{
    RenderSubmission* submission = _submissions.get(flightIndex);
    if (!submission || !submission->finish()) {
        // Sealing failed, so the command buffer must not be submitted: its
        // kept resources and finish state are what the fence slot expects, and
        // an empty present still legalizes the image the host acquired.
        YA_CORE_ERROR("Recording flight {} failed to seal its submission", flightIndex);
        return {};
    }

    return RecordedFrame{
        .commandBuffer = cmdBuf,
        .flightIndex   = submission->flightIndex(),
        .frameToken    = submission->frameToken(),
    };
}

} // namespace ya
