#include "RenderRuntime.h"

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

namespace
{

std::shared_ptr<RenderViewportOverlaySnapshot> buildViewportOverlaySnapshot(const CameraFrameInput::OverlayInput& overlay)
{
    auto snapshot = std::make_shared<RenderViewportOverlaySnapshot>();
    if (overlay.screenSprites) {
        snapshot->screenSprites = *overlay.screenSprites;
    }
    if (overlay.worldSprites) {
        snapshot->worldSprites = *overlay.worldSprites;
    }
    if (overlay.screenTexts) {
        snapshot->screenTexts = *overlay.screenTexts;
    }
    if (overlay.worldLines) {
        snapshot->worldLines = *overlay.worldLines;
    }
    return snapshot->empty() ? nullptr : snapshot;
}

} // namespace

void RenderRuntime::ensureViewportRectInitialized(const FrameInput& input)
{
    const Rect2D& rect = input.camera.viewportRect;
    if (rect.extent.x <= 0.0f || rect.extent.y <= 0.0f) {
        return;
    }

    const Rect2D& current = _viewportState.getRect();
    if (current.extent.x == rect.extent.x && current.extent.y == rect.extent.y &&
        current.pos.x == rect.pos.x && current.pos.y == rect.pos.y) {
        return;
    }

    onViewportResized(rect);
}

bool RenderRuntime::beginFrameCommandBuffer(const FrameInput& input, std::shared_ptr<ICommandBuffer>& cmdBuf)
{
    YA_PROFILE_SCOPE("RenderRuntime::beginFrameCommandBuffer");
    YA_PERF_SCOPE(perf::sample::renderPrepareFrame(), perf::metric::cpuTimeMs(), perf::domain::render());

    if (!input.present.surface || input.present.imageIndex < 0) {
        // Host skipped acquire (unpresentable / failed begin). One cmdBuf still
        // couples world record to present, so there is nothing to record here.
        // Camera skip is host policy, not a swapchain query inside RenderRuntime.
        return false;
    }

    const uint32_t flightIndex = input.camera.flightIndex;
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

    if (!_submissions.acquire(flightIndex, input.camera.frameIndex, cmdBuf.get(), input.present.surface)) {
        YA_CORE_ERROR("Recording flight {} failed to begin a live submission", flightIndex);
        return false;
    }
    if (!_viewOutputs.beginSubmission(flightIndex, input.camera.frameIndex)) {
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

void RenderRuntime::recordViewFamilies(
    const FrameInput& input,
    ICommandBuffer* cmdBuf,
    std::shared_ptr<RenderViewportOverlaySnapshot> overlaySnapshot)
{
    YA_PROFILE_FUNCTION();

    ISceneViewFamilyRenderer* pipeline = getActivePipeline();
    YA_CORE_ASSERT(pipeline, "Active render pipeline is null while recording a view family");

    RenderSubmission* live = _submissions.get(input.camera.flightIndex);
    YA_CORE_ASSERT(live && live->isRecording(), "Family record requires a recording submission");

    if (overlaySnapshot) {
        live->retain(overlaySnapshot);
        cmdBuf->retireResource(overlaySnapshot);
    }

    const auto recordOneFamily = [&](const SceneViewFamilyPlan* family, std::vector<SceneViewRecording> views) {
        ViewFamilyRecordContext ctx{
            .cmdBuf          = live->commandBuffer(),
            .hostCamera      = input.camera,
            .submission      = live,
            .plan            = input.sceneRender.plan,
            .family          = family,
            .views           = std::move(views),
            .overlaySnapshot = overlaySnapshot,
        };
        publishFamilyResult(input.camera.flightIndex, pipeline->recordFamily(ctx));
    };

    if (!input.sceneRender.views.empty() && input.sceneRender.plan && !input.sceneRender.plan->viewFamilies.empty()) {
        for (const SceneViewFamilyPlan& family : input.sceneRender.plan->viewFamilies) {
            std::vector<SceneViewRecording> familyViews;
            familyViews.reserve(family.viewportTaskIndices.size());
            for (uint32_t index : family.viewportTaskIndices) {
                if (index < input.sceneRender.views.size()) {
                    familyViews.push_back(input.sceneRender.views[index]);
                }
            }
            recordOneFamily(&family, std::move(familyViews));
        }
        return;
    }

    recordOneFamily(nullptr, input.sceneRender.views);
}

void RenderRuntime::renderWorldFrame(const FrameInput& input, ICommandBuffer* cmdBuf)
{
    YA_PROFILE_FUNCTION();

    auto overlaySnapshot = buildViewportOverlaySnapshot(input.camera.overlay);
    recordViewFamilies(input, cmdBuf, overlaySnapshot);

    if (const SceneViewportTask* displayRoot = input.sceneRender.primaryTask()) {
        if (displayRoot->viewId != 0) {
            _publishedOutputViewId = displayRoot->viewId;
            _publishedOutputFlight = input.camera.flightIndex;
        }
    }
}

std::shared_ptr<RenderTexture> RenderRuntime::getActiveViewportImageShared() const
{
    if (const auto* output = publishedViewOutput()) {
        if (output->color) {
            return output->color;
        }
    }
    return pipelineViewportColorImage();
}

std::shared_ptr<RenderTexture> RenderRuntime::pipelineViewportColorImage() const
{
    if (auto* pipeline = _pipelineCoordinator.getSelectedForwardPipeline()) {
        return pipeline->getViewportOutputImageShared();
    }
    if (auto* pipeline = _pipelineCoordinator.getSelectedDeferredPipeline()) {
        return pipeline->getViewportOutputImageShared();
    }
    return nullptr;
}

std::shared_ptr<RenderTexture> RenderRuntime::getViewportDisplayImageShared() const
{
    if (!_viewportState.isWorldSceneRenderEnabled()) {
        // World output is stale (or absent) while the world scene graph is
        // disabled; never present or composite a leftover image.
        return nullptr;
    }
    if (const auto* output = publishedViewOutput()) {
        if (auto image = output->displayImage()) {
            return image;
        }
    }
    return pipelineViewportDisplayImage();
}

std::shared_ptr<RenderTexture> RenderRuntime::pipelineViewportDisplayImage() const
{
    if (!_viewportState.isWorldSceneRenderEnabled()) {
        return nullptr;
    }
    if (auto postprocessOutput = getPostprocessOutputImageShared()) {
        return postprocessOutput;
    }
    return pipelineViewportColorImage();
}

const RenderViewOutput* RenderRuntime::publishedViewOutput() const
{
    if (_publishedOutputFlight >= MAX_FLIGHTS_IN_FLIGHT || _publishedOutputViewId == 0) {
        return nullptr;
    }
    return _viewOutputs.find(_publishedOutputFlight, _publishedOutputViewId);
}

const RenderViewOutput* RenderRuntime::getViewOutput(uint64_t viewId) const
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

void RenderRuntime::publishFamilyResult(uint32_t flightIndex, ViewFamilyRenderResult familyResult)
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

void RenderRuntime::retainPublishedViewOutputs(uint32_t flightIndex, ICommandBuffer* cmdBuf)
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

EFormat::T RenderRuntime::getViewportDisplayImageFormat() const
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

void RenderRuntime::endFrameCommandBuffer(ICommandBuffer* cmdBuf)
{
    YA_PROFILE_FUNCTION();

    if (YA_PERF_IS_ENABLED()) {
        YA_PROFILE_SCOPE("RenderRuntime::endGpuTiming");
        _render->endFrameGpuTiming(cmdBuf);
    }

    {
        YA_PROFILE_SCOPE("RenderRuntime::endCommandBuffer");
        cmdBuf->end();
    }

    if (YA_PERF_IS_ENABLED()) {
        YA_PROFILE_SCOPE("RenderRuntime::publishGpuMetrics");
        PerfState::get().setValue(
            perf::sample::renderFrame(),
            perf::metric::gpuTimeMs(),
            _render->getLastCompletedFrameGpuTimeMs(),
            perf::domain::gpu());
    }
}

} // namespace ya
