#include "RenderRuntime.h"

#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Graph/RenderGraphImportUtils.h"
#include "RHI/Core/RenderTexture.h"
#include "RHI/Core/Swapchain.h"
#include "Render3D/Deferred/DeferredRenderPipeline.h"
#include "Render3D/Forward/ForwardRenderPipeline.h"
#include "utility.cc/ranges.h"
#include <glm/gtc/matrix_transform.hpp>

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

    return true;
}

void RenderRuntime::beginViewportPassAndTickPipeline(const FrameInput& input, ICommandBuffer* cmdBuf)
{
    YA_PROFILE_FUNCTION();

    auto* pipeline = getActivePipeline();
    YA_CORE_ASSERT(pipeline, "Active render pipeline is null while ticking viewport pass");

    auto overlaySnapshot = buildViewportOverlaySnapshot(input.camera.overlay);
    pipeline->tick(RenderPipelineFrameContext{
        .cmdBuf                    = cmdBuf,
        .camera                    = input.camera,
        .viewportOverlaySnapshot   = std::move(overlaySnapshot),
    });
}

std::shared_ptr<RenderTexture> RenderRuntime::getActiveViewportImageShared() const
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
    if (auto postprocessOutput = getPostprocessOutputImageShared()) {
        return postprocessOutput;
    }
    return getActiveViewportImageShared();
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
