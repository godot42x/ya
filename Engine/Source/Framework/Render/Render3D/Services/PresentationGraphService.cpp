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
#include "Render3D/Pipelines/BasicPostprocessing.h"

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

/// Display compose copies a display-ready image onto the swapchain, so it runs
/// the postprocessor in its pass-through configuration: every grading stage off.
/// The input is a View's display image, which the finalize pass already gave its
/// output encoding and the view's grading. Grading it again here is the
/// double-grade this constant replaced: the windowed image used to receive an
/// ACES curve plus a second gamma on top of the view's own finalize, while the
/// editor viewport (which samples the display image directly) received one.
const PostProcessingState kDisplayComposeState = PostProcessingState::passThrough();

} // namespace

void PresentationGraphService::init(const InitDesc& desc)
{
    YA_CORE_ASSERT(desc.render != nullptr, "PresentationGraphService requires a render backend");
    YA_CORE_ASSERT(desc.present != nullptr, "PresentationGraphService requires a present surface");
    YA_CORE_ASSERT(desc.present->getSwapchain() != nullptr, "PresentationGraphService requires a swapchain");

    _render                       = desc.render;
    _present                      = desc.present;
    _viewDisplayImageProvider = desc.viewDisplayImageProvider;

    rebuildImages();

    auto* swapchain = _present->getSwapchain();
    _presentationPostProcessor = ya::makeShared<BasicPostprocessing>();
    _presentationPostProcessor->init(BasicPostprocessing::InitDesc{
        .render                = _render,
        .renderPass            = nullptr,
        .pipelineRenderingInfo = PipelineRenderingInfo{
            .label                   = "RuntimePresentation",
            .viewMask                = 0,
            .colorAttachmentFormats  = {swapchain->getFormat()},
            .depthAttachmentFormat   = EFormat::Undefined,
            .stencilAttachmentFormat = EFormat::Undefined,
        },
    });

    auto inputLayout = _presentationPostProcessor->getInputDSL();
    YA_CORE_ASSERT(inputLayout, "Presentation postprocessor must expose an input DSL");
    _presentationInputPool = IDescriptorPool::create(
        _render,
        DescriptorPoolCreateInfo{
            .label     = "Presentation_ToneMap_DSP",
            .maxSets   = 1,
            .poolSizes = {
                DescriptorPoolSize{
                    .type            = EPipelineDescriptorType::CombinedImageSampler,
                    .descriptorCount = 1,
                },
            },
        });
    YA_CORE_ASSERT(_presentationInputPool, "PresentationGraphService requires a tone-map descriptor pool");
    _presentationToneMap.input.set = _presentationInputPool->allocateDescriptorSets(inputLayout);
    YA_CORE_ASSERT(_presentationToneMap.input.set,
                   "PresentationGraphService failed to allocate the swapchain blit descriptor set");

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
    _presentationToneMap = {};
    _presentationInputPool.reset();
    if (_presentationPostProcessor) {
        _presentationPostProcessor->shutdown();
        _presentationPostProcessor.reset();
    }
    _presentationGraphExecutors.clear();
    _presentationImages.clear();
    _viewDisplayImageProvider = {};
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
        _presentationGraphExecutors.push_back(std::make_unique<RenderGraphExecutor>(*factory));
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

void PresentationGraphService::recordDisplayCompose(float                     deltaTime,
                                                    IFrameRecordExtensions* extensions,
                                                    ICommandBuffer*         cmdBuf)
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

    if (extensions) {
        // Contract: this hook runs before the presentation graph is built and
        // recorded, inside the already-open frame command buffer. Content
        // recorded here (e.g. ImGui draw data consumed later by the graph) must
        // not recreate GPU resources; layout transitions must go through the
        // shared resource state tracker.
        extensions->recordBeforeDisplayExtensions(*cmdBuf, deltaTime);
    }

    const Extent2D presentationExtent = presentationImage->getExtent();
    auto           sourceImage        = _viewDisplayImageProvider ? _viewDisplayImageProvider() : nullptr;
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
        [this, sourceImage, output, presentationExtent, extensions, deltaTime](RGRenderContext& rgCtx)
        {
            [[maybe_unused]] const auto rasterParams = rgCtx.getRasterPassExecutionParams();
            rgCtx.beginDeclaredRasterRendering();

            if (_presentationPostProcessor && sourceImage && sourceImage->getImageView()) {
                _presentationPostProcessor->render(BasicPostprocessing::RenderDesc{
                    .cmdBuf         = &rgCtx.getCommandBuffer(),
                    .ctx            = nullptr,
                    .inputImageView = sourceImage->getImageView(),
                    .renderExtent   = presentationExtent,
                    .bOutputIsSRGB  = EFormat::isSRGB(getSwapchain() ? getSwapchain()->getFormat() : EFormat::Undefined),
                    .state          = &kDisplayComposeState,
                    .toneMap        = _presentationToneMap,
                });
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
