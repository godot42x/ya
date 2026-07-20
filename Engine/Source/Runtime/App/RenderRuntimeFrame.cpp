#include "RenderRuntime.h"

#include "App.h"
#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Core/UI/UIManager.h"
#include "DeferredRender/DeferredRenderPipeline.h"
#include "Render/2D/Render2D.h"
#include "Render/Core/RenderGraphImportUtils.h"
#include "Resource/Font/FontManager.h"
#include "Runtime/App/ForwardRender/ForwardRenderPipeline.h"
#include <functional>
#include "utility.cc/ranges.h"

#include <glm/gtc/matrix_transform.hpp>

namespace ya
{

namespace
{

RGImportedTextureDesc makePresentationImportedTextureDesc(const RenderImage& image,
                                                          std::string_view label,
                                                          EImageLayout::T finalLayout)
{
    auto desc = makeImportedTextureDesc(image, label, finalLayout, EImageUsage::ColorAttachment);
    desc.importDesc.initialLayout = EImageLayout::PresentSrcKHR;
    return desc;
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
    auto* pipeline = getActivePipelineExecution();
    YA_CORE_ASSERT(pipeline, "Active render pipeline is null while ticking viewport pass");

    const auto overlayInput = input.overlay;
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
        .recordViewportOverlays   = [this, overlayInput, input](ICommandBuffer* overlayCmdBuf, Extent2D viewportExtent, const FrameContext& frameCtx) {
            RenderPipelineFrameContext overlayFrame = input.pipeline;
            overlayFrame.cmdBuf = overlayCmdBuf;
            overlayFrame.view = frameCtx.view;
            overlayFrame.projection = frameCtx.projection;
            overlayFrame.cameraPos = frameCtx.cameraPos;
            overlayFrame.viewportRect = Rect2D{
                .pos    = {0.0f, 0.0f},
                .extent = {static_cast<float>(viewportExtent.width), static_cast<float>(viewportExtent.height)},
            };
            renderViewportPassOverlays(overlayFrame, overlayInput, overlayCmdBuf);
        },
    });
}

std::shared_ptr<RenderImage> RenderRuntime::getActiveViewportImageShared() const
{
    if (_renderPipeline == ERenderPipeline::Forward && _forwardPipeline) {
        return _forwardPipeline->getViewportOutputImageShared();
    }
    if (_renderPipeline == ERenderPipeline::Deferred && _deferredPipeline) {
        return _deferredPipeline->getViewportOutputImageShared();
    }
    return nullptr;
}

void RenderRuntime::renderViewportPassOverlays(const RenderPipelineFrameContext& pipelineFrame, const FrameInput::OverlayInput& overlay, ICommandBuffer* cmdBuf)
{
    YA_PROFILE_SCOPE("Render2D");
    YA_PERF_SCOPE(perf::sample::renderViewportOverlay(), perf::metric::cpuTimeMs(), perf::domain::render());

    const Extent2D viewportExtent = Extent2D{
        .width  = static_cast<uint32_t>(pipelineFrame.viewportRect.extent.x),
        .height = static_cast<uint32_t>(pipelineFrame.viewportRect.extent.y),
    };
    FRender2dContext render2dCtx{
        .cmdBuf       = cmdBuf,
        .windowWidth  = viewportExtent.width,
        .windowHeight = viewportExtent.height,
        .cam          = {
            .position       = pipelineFrame.cameraPos,
            .view           = pipelineFrame.view,
            .projection     = pipelineFrame.projection,
            .viewProjection = pipelineFrame.projection * pipelineFrame.view,
        },
    };

    Render2D::begin(render2dCtx);

    if (overlay.screenSprites) {
        for (const auto& sprite : *overlay.screenSprites) {
            Render2D::makeSprite(glm::vec3(sprite.viewportPos, 0.0f), sprite.size, sprite.texture, sprite.tint);
        }
    }

    if (overlay.worldSprites) {
        for (const auto& sprite : *overlay.worldSprites) {
            Render2D::makeWorldSprite(sprite.worldTransform, sprite.texture, sprite.tint);
        }
    }

    if (overlay.screenTexts) {
        for (const auto& text : *overlay.screenTexts) {
            if (text.text.empty()) {
                continue;
            }

            auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, text.fontSize);
            if (!font) {
                continue;
            }

            Render2D::makeText(text.text,
                               glm::vec3(text.viewportPos, text.depth),
                               text.color,
                               font.get());
        }
    }

    Render2D::onRender();
    UIManager::get()->render();
    Render2D::end();
}

void RenderRuntime::renderPresentationPass(float deltaTime,
                                           const std::function<void(ICommandBuffer*)>& recordBeforePresentationExtensions,
                                           const std::function<void(ICommandBuffer*)>& recordPresentationExtensions,
                                           const std::function<void(ICommandBuffer*)>& recordPresentationCapture,
                                           ICommandBuffer* cmdBuf)
{
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

    if (recordBeforePresentationExtensions) {
        recordBeforePresentationExtensions(cmdBuf);
    }

    const Extent2D presentationExtent = presentationImage->getExtent();
    RenderGraph    graph;
    const auto     output = graph.importTexture(
        makePresentationImportedTextureDesc(*presentationImage,
                                            "Presentation.Output",
                                            EImageLayout::PresentSrcKHR));

    [[maybe_unused]] const auto pass = graph.addPass(
        "Presentation",
        [output](RGPassBuilder& passBuilder) {
            passBuilder.useColorAttachment(output);
        },
        [output, presentationExtent, recordPresentationExtensions](RGRenderContext& rgCtx) {
            rgCtx.beginColorRendering({
                .color = output,
                .renderArea = Rect2D{
                    .pos    = {0.0f, 0.0f},
                    .extent = presentationExtent.toVec2(),
                },
                .clearValue  = ClearValue::Black(),
                .finalLayout = EImageLayout::PresentSrcKHR,
            });

            if (recordPresentationExtensions) {
                recordPresentationExtensions(&rgCtx.getCommandBuffer());
            }

            rgCtx.endRendering();
        });

    [[maybe_unused]] const bool bExecuted = presentationExecutor->execute(graph, *cmdBuf);
    if (recordPresentationCapture) {
        recordPresentationCapture(cmdBuf);
    }
}

void RenderRuntime::submitFrame(int32_t imageIndex, ICommandBuffer* cmdBuf)
{
    if (YA_PERF_IS_ENABLED()) {
        _render->endFrameGpuTiming(cmdBuf);
    }
    cmdBuf->end();
    _render->end(imageIndex, {cmdBuf->getHandle()});

    if (YA_PERF_IS_ENABLED()) {
        PerfState::Get().setValue(
            perf::sample::renderFrame(),
            perf::metric::gpuTimeMs(),
            _render->getLastCompletedFrameGpuTimeMs(),
            perf::domain::gpu());
    }
}

} // namespace ya
