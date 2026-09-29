#include "Render3D/Services/PresentationGraphService.h"

#include "Core/Log.h"
#include "Core/Profiling/Instrumentor.h"
#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Core/Profiling/Profiling.h"
#include "Graph/RenderGraph.h"
#include "Graph/RenderGraphExecutor.h"
#include "Graph/RenderGraphImportUtils.h"
#include "RHI/Core/RenderResourceFactory.h"
#include "RHI/Core/RenderSurfaceContext.h"
#include "RHI/Core/RenderTexture.h"
#include "RHI/Core/Swapchain.h"
#include "RHI/Render.h"

#include <limits>

namespace ya
{

namespace
{

RGImportedTextureDesc makePresentationImportedTextureDesc(const RenderTexture& image,
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

} // namespace

void PresentationGraphService::init(const InitDesc& desc)
{
    YA_CORE_ASSERT(desc.render != nullptr, "PresentationGraphService requires a render backend");
    YA_CORE_ASSERT(desc.present != nullptr, "PresentationGraphService requires a present surface");
    YA_CORE_ASSERT(desc.present->getSwapchain() != nullptr, "PresentationGraphService requires a swapchain");

    _render                       = desc.render;
    _present                      = desc.present;
    _backdropWriter               = desc.backdropWriter;

    rebuildImages();

    auto* swapchain = _present->getSwapchain();

    swapchain->onRecreate.addLambda(
        this,
        [this](ISwapchain::DiffInfo old, ISwapchain::DiffInfo now, bool bImageRecreated)
        {
            const bool bExtentChanged = (now.extent.width != old.extent.width ||
                                         now.extent.height != old.extent.height);
            const bool bPresentModeChanged = (old.presentMode != now.presentMode);

            if (bExtentChanged || bImageRecreated || bPresentModeChanged) {
                rebuildImages();
            }
        });
}

void PresentationGraphService::shutdown()
{
    if (_present) {
        if (auto* swapchain = _present->getSwapchain()) {
            swapchain->onRecreate.removeAll(this);
        }
    }
    _backdropWriter = nullptr;
    _presentationGraphExecutors.clear();
    _presentationImages.clear();
    _present = nullptr;
    _render  = nullptr;
}

void PresentationGraphService::rebuildImages()
{
    for (auto& executor : _presentationGraphExecutors) {
        if (executor) {
            executor->clear();
        }
    }
    _presentationGraphExecutors.clear();

    _presentationImages.clear();
    if (!_render || !_present) {
        return;
    }

    IRenderResourceFactory* factory = _render->getResourceFactory();
    if (!factory) {
        return;
    }
    if (!_present->buildPresentationImages(*factory, "Presentation", _presentationImages)) {
        YA_CORE_ERROR("PresentationGraphService: failed to import presentation images");
        return;
    }
    _presentationGraphExecutors.reserve(_presentationImages.size());
    for (size_t i = 0; i < _presentationImages.size(); ++i) {
        _presentationGraphExecutors.push_back(
            std::make_unique<RenderGraphExecutor>(*factory, std::format("Presentation[{}]", i)));
    }
}

ISwapchain* PresentationGraphService::getSwapchain() const
{
    return _present ? _present->getSwapchain() : nullptr;
}

uint32_t PresentationGraphService::getCurrentPresentationImageIndex() const
{
    auto* swapchain = getSwapchain();
    if (!swapchain) {
        return std::numeric_limits<uint32_t>::max();
    }

    return swapchain->getCurImageIndex();
}

std::shared_ptr<RenderTexture> PresentationGraphService::getCurrentPresentationImageShared() const
{
    const auto imageIndex = getCurrentPresentationImageIndex();
    if (imageIndex >= _presentationImages.size()) {
        return nullptr;
    }

    return _presentationImages[imageIndex];
}

void PresentationGraphService::recordDisplayCompose(const FSurfaceImage&    backdrop,
                                                    RenderSubmission&       submission,
                                                    float                   deltaTime,
                                                    IFrameRecordExtensions* extensions,
                                                    ICommandBuffer*         cmdBuf,
                                                    int32_t                 imageIndex)
{
    YA_PROFILE_FUNCTION();

    YA_PROFILE_SCOPE("Screen pass");
    YA_PERF_SCOPE(perf::sample::renderPresentation(), perf::metric::cpuTimeMs(), perf::domain::render());

    if (!cmdBuf) {
        return;
    }

    // The acquired token the plan carries is the only answer to "which image
    // this record writes"; it is never re-derived from the swapchain here.
    if (imageIndex < 0 || static_cast<size_t>(imageIndex) >= _presentationGraphExecutors.size()) {
        return;
    }
    auto* presentationExecutor = _presentationGraphExecutors[static_cast<size_t>(imageIndex)].get();
    if (!presentationExecutor) {
        return;
    }

    auto presentationImage = _presentationImages[static_cast<size_t>(imageIndex)];
    if (!presentationImage) {
        return;
    }
    if (extensions) {
        // Contract: this hook runs before the presentation graph is built and
        // recorded, inside the already-open frame command buffer. Content
        // recorded here (e.g. ImGui draw data consumed later by the graph) must
        // not recreate GPU resources; layout transitions must go through the
        // shared resource state tracker.
        extensions->recordBeforeDisplayExtensions(*cmdBuf, deltaTime);
    }

    const Extent2D presentationExtent = presentationImage->getExtent();

    // The gate: the surface's format may apply the transfer function to whatever
    // this pass writes, so an image that already carries it would come out
    // encoded twice. Refused and reported rather than written, because "we
    // presented the wrong image" has no visible symptom other than wrong
    // colours, and a surface quietly re-grading is exactly how that shipped
    // before. The pass still runs: the surface keeps its clear and the host's
    // own content is unaffected.
    IImageView* backdropImageView = nullptr;
    if (backdrop.hasImage()) {
        const EFormat::T surfaceFormat = getSwapchain() ? getSwapchain()->getFormat() : EFormat::Undefined;
        if (const char* mismatch = findSurfaceImageMismatch(backdrop, surfaceFormat)) {
            YA_CORE_ERROR("PresentationGraphService: refusing the surface image: {}. Surface format is {}",
                          mismatch,
                          std::to_string(surfaceFormat));
        }
        else {
            backdropImageView = backdrop.image->getImageView();
        }
    }

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
                    .clearValue  = ClearValue::black(),
                    .finalLayout = EImageLayout::PresentSrcKHR,
                }},
            });
        },
        [this, backdropImageView, &submission, output, presentationExtent, extensions, deltaTime](RGRenderContext& rgCtx)
        {
            [[maybe_unused]] const auto rasterParams = rgCtx.getRasterPassExecutionParams();
            rgCtx.beginDeclaredRasterRendering();

            // The renderer's pass, not this service's: presentation owns the
            // surface and the order, and nothing about how an image is drawn.
            if (_backdropWriter && backdropImageView) {
                _backdropWriter->writeSurfaceBackdrop(rgCtx.getCommandBuffer(),
                                                      submission,
                                                      *backdropImageView,
                                                      presentationExtent);
            }

            if (extensions) {
                extensions->recordDisplayExtensions(rgCtx.getCommandBuffer(), deltaTime);
            }

            rgCtx.endRendering();
        });

    if (extensions) {
        extensions->appendDisplayCapture(graph, output, presentationExtent);
    }

    [[maybe_unused]] const bool bExecuted = presentationExecutor->execute(graph, *cmdBuf);
}

} // namespace ya
