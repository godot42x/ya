#include "RenderRuntime.h"

#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Graph/RenderGraphImportUtils.h"
#include "RHI/Core/Swapchain.h"
#include "Host/App.h"
#include "Render3D/Deferred/DeferredRenderPipeline.h"
#include "Render3D/Forward/ForwardRenderPipeline.h"
#include "utility.cc/ranges.h"
#include <functional>

#include <glm/gtc/matrix_transform.hpp>

namespace ya
{

namespace
{

RGImportedTextureDesc makePresentationImportedTextureDesc(const RenderImage& image,
                                                          std::string_view   label,
                                                          EImageLayout::T    finalLayout)
{
    auto desc                     = makeImportedTextureDesc(
        image,
        label,
        finalLayout,
        static_cast<EImageUsage::T>(EImageUsage::ColorAttachment | EImageUsage::TransferSrc));
    desc.importDesc.initialLayout = EImageLayout::PresentSrcKHR;
    return desc;
}

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
    if (_viewportRect.extent.x > 0 && _viewportRect.extent.y > 0) {
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
        .viewportRect             = _viewportRect,
        .viewportFrameBufferScale = _viewportFrameBufferScale,
        .frameData                = input.pipeline.frameData,
        .shadowSettings           = input.pipeline.shadowSettings,
        .viewportOverlaySnapshot   = std::move(overlaySnapshot),
    });
}

std::shared_ptr<RenderImage> RenderRuntime::getActiveViewportImageShared() const
{
    if (auto* pipeline = getSelectedForwardPipeline()) {
        return pipeline->getViewportOutputImageShared();
    }
    if (auto* pipeline = getSelectedDeferredPipeline()) {
        return pipeline->getViewportOutputImageShared();
    }
    return nullptr;
}

std::shared_ptr<RenderImage> RenderRuntime::getViewportDisplayImageShared() const
{
    if (!_bWorldSceneRenderEnabled) {
        // World output is stale (or absent) while the world scene graph is
        // disabled; never present or composite a leftover image.
        return nullptr;
    }
    if (auto postprocessOutput = getPostprocessOutputImageShared()) {
        return postprocessOutput;
    }
    return getActiveViewportImageShared();
}

void RenderRuntime::renderPresentationPass(float                              deltaTime,
                                           const PresentationExtensions&      presentationExtensions,
                                           ICommandBuffer*                    cmdBuf)
{
    YA_PROFILE_FUNCTION();

    YA_PROFILE_SCOPE("Screen pass");
    YA_PERF_SCOPE(perf::sample::renderPresentation(), perf::metric::cpuTimeMs(), perf::domain::render());

    if (!cmdBuf) {
        return;
    }

    const uint32_t presentationImageIndex = getCurrentPresentationImageIndex();
    if (presentationImageIndex >= _presentationGraphExecutors.size()) {
        return;
    }
    auto* presentationExecutor = _presentationGraphExecutors[presentationImageIndex].get();
    if (!presentationExecutor) {
        return;
    }

    auto presentationImage = getCurrentPresentationImageShared();
    if (!presentationImage) {
        return;
    }
    if (_presentationPostProcessor) {
        _presentationPostProcessor->beginFrame();
    }

    if (presentationExtensions.recordBeforeExtensions) {
        // Contract: this hook runs before the presentation graph is built and
        // recorded, inside the already-open frame command buffer. Content
        // recorded here (e.g. ImGui draw data consumed later by the graph) must
        // not recreate GPU resources; layout transitions must go through the
        // shared resource state tracker.
        presentationExtensions.recordBeforeExtensions(cmdBuf);
    }

    const Extent2D presentationExtent = presentationImage->getExtent();
    auto           sourceImage        = getViewportDisplayImageShared();
    RenderGraph graph;
    const auto  output = graph.importTexture(
        makePresentationImportedTextureDesc(*presentationImage,
                                            "Presentation.Output",
                                            EImageLayout::PresentSrcKHR));

    [[maybe_unused]] const auto pass = graph.addPass(
        "Presentation",
        [output, presentationExtent](RGPassBuilder& passBuilder)
        {
            passBuilder.declareRaster({
                .renderArea  = Rect2D{.pos = {0.0f, 0.0f}, .extent = presentationExtent.toVec2()},
                .layerCount  = 1,
                .colors = {{
                    .color       = output,
                    .clearValue  = ClearValue::Black(),
                    .finalLayout = EImageLayout::PresentSrcKHR,
                }},
            });
        },
        [this, sourceImage, output, presentationExtent, presentationExtensions, deltaTime](RGRenderContext& rgCtx)
        {
            [[maybe_unused]] const auto rasterParams = rgCtx.getRasterPassExecutionParams();
            rgCtx.beginDeclaredRasterRendering();

            if (_presentationPostProcessor && sourceImage && sourceImage->getImageView()) {
                _presentationPostProcessor->render(BasicPostprocessing::RenderDesc{
                    .cmdBuf         = &rgCtx.getCommandBuffer(),
                    .ctx            = nullptr,
                    .inputImageView = sourceImage->getImageView(),
                    .renderExtent   = presentationExtent,
                    .bOutputIsSRGB  = EFormat::isSRGB(_render->getSwapchain()->getFormat()),
                    .state          = &_presentationPostProcessState,
                });
            }

            if (presentationExtensions.recordExtensions) {
                presentationExtensions.recordExtensions(&rgCtx.getCommandBuffer());
            }

            rgCtx.endRendering();
        });

    if (presentationExtensions.appendCapture) {
        presentationExtensions.appendCapture(graph, output, presentationExtent);
    }

    [[maybe_unused]] const bool bExecuted = presentationExecutor->execute(graph, *cmdBuf);
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
