#include "RenderDeviceState.h"

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

    const uint32_t flightIndex = plan.camera.flightIndex;
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

    if (!_submissions.acquire(flightIndex, plan.camera.frameIndex, cmdBuf.get(), plan.present.surface)) {
        YA_CORE_ERROR("Recording flight {} failed to begin a live submission", flightIndex);
        return false;
    }
    if (!_viewOutputs.beginSubmission(flightIndex, plan.camera.frameIndex)) {
        YA_CORE_ERROR("Recording flight {} failed to begin view outputs", flightIndex);
        return false;
    }
    if (_publishedOutputFlight == flightIndex &&
        _viewOutputs.find(flightIndex, _publishedOutputViewId) == nullptr) {
        _publishedOutputFlight = MAX_FLIGHTS_IN_FLIGHT;
        _publishedOutputViewId = 0;
    }

    return true;
}

void RenderDeviceState::clearPublishedViewOutputs()
{
    _publishedOutputFlight = MAX_FLIGHTS_IN_FLIGHT;
    _publishedOutputViewId = 0;
}

std::shared_ptr<RenderTexture> RenderDeviceState::getActiveViewportImageShared() const
{
    if (const auto* output = publishedViewOutput()) {
        if (output->color) {
            return output->color;
        }
    }
    return pipelineViewportColorImage();
}

std::shared_ptr<RenderTexture> RenderDeviceState::pipelineViewportColorImage() const
{
    if (auto* pipeline = _pipelineCoordinator.getSelectedForwardPipeline()) {
        return pipeline->getViewportOutputImageShared();
    }
    if (auto* pipeline = _pipelineCoordinator.getSelectedDeferredPipeline()) {
        return pipeline->getViewportOutputImageShared();
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

std::shared_ptr<RenderTexture> RenderDeviceState::pipelineViewportDisplayImage() const
{
    if (auto postprocessOutput = getPostprocessOutputImageShared()) {
        return postprocessOutput;
    }
    return pipelineViewportColorImage();
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
    if (_publishedOutputFlight < MAX_FLIGHTS_IN_FLIGHT) {
        if (const auto* output = _viewOutputs.find(_publishedOutputFlight, viewId)) {
            return output;
        }
    }
    for (uint32_t flightIndex = 0; flightIndex < MAX_FLIGHTS_IN_FLIGHT; ++flightIndex) {
        if (const auto* output = _viewOutputs.find(flightIndex, viewId)) {
            return output;
        }
    }
    return nullptr;
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
            continue;
        }
        _publishedOutputFlight = flightIndex;
        _publishedOutputViewId = viewId;
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
            perf::sample::renderFrame(),
            perf::metric::gpuTimeMs(),
            _render->getLastCompletedFrameGpuTimeMs(),
            perf::domain::gpu());
    }
}

} // namespace ya
