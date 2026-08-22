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

std::shared_ptr<RenderViewportOverlaySnapshot> buildViewportOverlaySnapshot(const RenderRuntime::FrameInput::OverlayInput& overlay)
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
    if (_viewportState.getRect().extent.x > 0 && _viewportState.getRect().extent.y > 0) {
        return;
    }

    if (input.pipeline.viewportRect.extent.x > 0 && input.pipeline.viewportRect.extent.y > 0) {
        onViewportResized(input.pipeline.viewportRect);
        return;
    }

    auto swapchainExtent = _render->getSwapchain()->getExtent();
    onViewportResized(Rect2D{
        .pos    = {0.0f, 0.0f},
        .extent = {static_cast<float>(swapchainExtent.width), static_cast<float>(swapchainExtent.height)},
    });
}

bool RenderRuntime::beginFrameCommandBuffer(int32_t& imageIndex, std::shared_ptr<ICommandBuffer>& cmdBuf)
{
    YA_PROFILE_SCOPE("RenderRuntime::beginFrameCommandBuffer");
    YA_PERF_SCOPE(perf::sample::renderPrepareFrame(), perf::metric::cpuTimeMs(), perf::domain::render());

    if (_render->getSwapchain()->getExtent().width <= 0 || _render->getSwapchain()->getExtent().height <= 0) {
        return false;
    }

    imageIndex = -1;
    {
        YA_PERF_SCOPE(perf::sample::renderBegin(), perf::metric::cpuTimeMs(), perf::domain::render());
        if (!_render->begin(&imageIndex)) {
            return false;
        }
    }
    if (imageIndex < 0) {
        YA_CORE_WARN("Invalid image index ({}), skipping frame render", imageIndex);
        return false;
    }

    cmdBuf = _commandBuffers[imageIndex];
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

    auto overlaySnapshot = buildViewportOverlaySnapshot(input.overlay);
    pipeline->tick(RenderPipelineFrameContext{
        .flightIndex              = input.pipeline.flightIndex,
        .cmdBuf                   = cmdBuf,
        .deltaTime                = input.pipeline.deltaTime,
        .view                     = input.pipeline.view,
        .projection               = input.pipeline.projection,
        .cameraPos                = input.pipeline.cameraPos,
        .viewportRect             = _viewportState.getRect(),
        .viewportFrameBufferScale = _viewportState.getFrameBufferScale(),
        .frameData                = input.pipeline.frameData,
        .shadowSettings           = input.pipeline.shadowSettings,
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

void RenderRuntime::submitFrame(int32_t imageIndex, ICommandBuffer* cmdBuf)
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

    {
        YA_PROFILE_SCOPE("RenderRuntime::present");
        _render->end(imageIndex, {cmdBuf->getHandle()});
    }

    if (YA_PERF_IS_ENABLED()) {
        YA_PROFILE_SCOPE("RenderRuntime::publishGpuMetrics");
        PerfState::Get().setValue(
            perf::sample::renderFrame(),
            perf::metric::gpuTimeMs(),
            _render->getLastCompletedFrameGpuTimeMs(),
            perf::domain::gpu());
    }
}

} // namespace ya
