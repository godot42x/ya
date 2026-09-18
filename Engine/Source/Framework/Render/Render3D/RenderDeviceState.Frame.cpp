#include "Render3D/RenderDeviceState.h"

#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Graph/RenderGraphImportUtils.h"
#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/RenderTexture.h"
#include "RHI/Core/Swapchain.h"
#include "Render3D/Deferred/DeferredRenderPipeline.h"
#include "Render3D/Forward/ForwardRenderPipeline.h"
#include "utility.cc/ranges.h"
#include <glm/gtc/matrix_transform.hpp>
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

void RenderDeviceState::clearPublishedViewOutputs()
{
    _publishedOutputFlight = MAX_FLIGHTS_IN_FLIGHT;
    _publishedOutputViewId = 0;
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

void RenderDeviceState::publishViewOutputIdentity(uint32_t flightIndex, SceneViewId displayViewId)
{
    // The one place that decides which View's output the host viewport shows.
    // The plan names it, so the answer is not "whichever family was recorded
    // last", and a tick that declared no display root clears the identity
    // instead of leaving the previous tick's View readable as if this frame had
    // produced it. Readers go through publishedViewOutput(), which resolves the
    // identity against this flight's table, so a stale identity cannot outlive
    // the outputs it names.
    if (displayViewId == 0) {
        clearPublishedViewOutputs();
        return;
    }

    _publishedOutputFlight = flightIndex;
    _publishedOutputViewId = displayViewId;
}

std::shared_ptr<RenderTexture> RenderDeviceState::getActiveViewportImageShared() const
{
    // The host viewport's colour, and nothing else: this is the View the plan
    // named as its display root, so a tick that published none has no viewport
    // image. Falling back to "whatever the pipeline last published" would answer
    // a question about a different frame with a plausible-looking image.
    if (const auto* output = publishedViewOutput()) {
        return output->color;
    }
    return nullptr;
}

std::shared_ptr<RenderTexture> RenderDeviceState::getViewportDisplayImageShared() const
{
    if (const auto* output = publishedViewOutput()) {
        if (auto image = output->displayImage()) {
            return image;
        }
    }
    return nullptr;
}

const RenderViewOutput* RenderDeviceState::publishedViewOutput() const
{
    if (_publishedOutputFlight >= MAX_FLIGHTS_IN_FLIGHT || _publishedOutputViewId == 0) {
        return nullptr;
    }
    return _viewOutputs.find(_publishedOutputFlight, _publishedOutputViewId);
}

const RenderViewOutput* RenderDeviceState::getViewOutput(uint64_t viewId) const
{
    if (viewId == 0) {
        return nullptr;
    }
    // This flight only. The question is "the View this frame recorded", and
    // scanning other flights answers it with a View from another frame that
    // happens to share an id -- which is exactly how a stale image gets shown as
    // if it were current. A consumer that genuinely needs an older image (the
    // viewport debug catalog) holds its own handle instead of searching here.
    if (_publishedOutputFlight >= MAX_FLIGHTS_IN_FLIGHT) {
        return nullptr;
    }
    return _viewOutputs.find(_publishedOutputFlight, viewId);
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
    }
}

EFormat::T RenderDeviceState::getViewportDisplayImageFormat() const
{
    // Mirrors getViewportDisplayImageShared(): post-process output when
    // postprocessing runs, else the raw viewport image. Both formats are
    // pipeline-configured and stable, so they are known before the world graph
    // creates the actual images (first-frame Render2D pipeline prep).
    if (auto* pipeline = _pipelineCoordinator.getSelectedForwardPipeline()) {
        return pipeline->isPostprocessingEnabled() ? pipeline->getPostprocessColorFormat()
                                                   : pipeline->getViewportColorFormat();
    }
    if (auto* pipeline = _pipelineCoordinator.getSelectedDeferredPipeline()) {
        return pipeline->isPostprocessingEnabled() ? pipeline->getPostprocessColorFormat()
                                                   : pipeline->getViewportColorFormat();
    }
    return EFormat::Undefined;
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

} // namespace ya
