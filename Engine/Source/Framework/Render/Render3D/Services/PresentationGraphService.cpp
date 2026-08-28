#include "PresentationGraphService.h"

#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Core/Profiling/Profiling.h"
#include "Graph/RenderGraph.h"
#include "Graph/RenderGraphExecutor.h"
#include "Graph/RenderGraphImportUtils.h"
#include "RHI/Backend/Vulkan/VulkanSwapChain.h"
#include "RHI/Core/RenderTexture.h"
#include "RHI/Core/Swapchain.h"
#include "Render3D/Pipelines/BasicPostprocessing.h"

#include <format>
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

std::shared_ptr<RenderTexture> createPresentationRenderTexture(IRender& render, VulkanSwapChain& swapchain, uint32_t imageIndex)
{
    const auto& swapchainCI = swapchain.getCreateInfo();
    auto importedImage = render.getResourceFactory()->importImage(ImportedImageDesc{
        .label         = std::format("Presentation_{}", imageIndex),
        .nativeHandle  = static_cast<void*>(swapchain.getVkImages().at(imageIndex)),
        .format        = swapchain.getFormat(),
        .usage         = static_cast<EImageUsage::T>(EImageUsage::ColorAttachment |
                    (swapchainCI.bEnableTransferSrc ? EImageUsage::TransferSrc : EImageUsage::None)),
        .extent        = {.width = swapchain.getExtent().width, .height = swapchain.getExtent().height, .depth = 1},
        .initialLayout = EImageLayout::Undefined,
        .finalLayout   = EImageLayout::PresentSrcKHR,
    });
    YA_CORE_ASSERT(importedImage != nullptr, "Failed to import presentation image {}", imageIndex);

    auto imageView = render.getResourceFactory()->createImageView(
        importedImage,
        ImageViewCreateInfo{
            .label          = std::format("Presentation_{}_View", imageIndex),
            .viewType       = EImageViewType::View2D,
            .aspectFlags    = EImageAspect::Color,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        });
    YA_CORE_ASSERT(imageView != nullptr, "Failed to create presentation image view {}", imageIndex);

    auto resource = std::make_shared<ImageResource>();
    resource->label       = std::format("Presentation_{}", imageIndex);
    resource->desc.image  = ImageCreateInfo{
        .label   = resource->label,
        .format  = swapchain.getFormat(),
        .extent  = {.width = swapchain.getExtent().width, .height = swapchain.getExtent().height, .depth = 1},
        .mipLevels   = 1,
        .arrayLayers = 1,
        .samples     = ESampleCount::Sample_1,
        .usage   = static_cast<EImageUsage::T>(EImageUsage::ColorAttachment |
                    (swapchainCI.bEnableTransferSrc ? EImageUsage::TransferSrc : EImageUsage::None)),
    };
    resource->desc.defaultView = ImageViewCreateInfo{
        .label          = std::format("Presentation_{}_View", imageIndex),
        .viewType       = EImageViewType::View2D,
        .aspectFlags    = EImageAspect::Color,
        .baseMipLevel   = 0,
        .levelCount     = 1,
        .baseArrayLayer = 0,
        .layerCount     = 1,
    };
    resource->image       = std::move(importedImage);
    resource->defaultView = std::move(imageView);
    return RenderTexture::adopt(std::move(resource));
}

} // namespace

void PresentationGraphService::init(const InitDesc& desc)
{
    YA_CORE_ASSERT(desc.render != nullptr, "PresentationGraphService requires a render backend");

    _render                      = desc.render;
    _viewportDisplayImageProvider = desc.viewportDisplayImageProvider;

    rebuildImages();

    _presentationPostProcessor = ya::makeShared<BasicPostprocessing>();
    _presentationPostProcessor->init(BasicPostprocessing::InitDesc{
        .render                = _render,
        .renderPass            = nullptr,
        .pipelineRenderingInfo = PipelineRenderingInfo{
            .label                   = "RuntimePresentation",
            .viewMask                = 0,
            .colorAttachmentFormats  = {_render->getSwapchain()->getFormat()},
            .depthAttachmentFormat   = EFormat::Undefined,
            .stencilAttachmentFormat = EFormat::Undefined,
        },
    });

    _render->getSwapchain()->onRecreate.addLambda(
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
    if (_presentationPostProcessor) {
        _presentationPostProcessor->shutdown();
        _presentationPostProcessor.reset();
    }
    _presentationGraphExecutors.clear();
    _presentationImages.clear();
    _presentationPostProcessState = {};
    _viewportDisplayImageProvider = {};
    _render = nullptr;
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
    if (!_render) {
        return;
    }

    auto* swapchain = _render->getSwapchain() ? _render->getSwapchain()->as<VulkanSwapChain>() : nullptr;
    YA_CORE_ASSERT(swapchain != nullptr, "Presentation resources currently require VulkanSwapChain");

    _presentationGraphExecutors.reserve(swapchain->getImageCount());
    _presentationImages.reserve(swapchain->getImageCount());
    for (uint32_t imageIndex = 0; imageIndex < swapchain->getImageCount(); ++imageIndex) {
        _presentationGraphExecutors.push_back(std::make_unique<RenderGraphExecutor>(*_render->getResourceFactory()));
        _presentationImages.push_back(createPresentationRenderTexture(*_render, *swapchain, imageIndex));
    }
}

uint32_t PresentationGraphService::getCurrentPresentationImageIndex() const
{
    if (!_render) {
        return std::numeric_limits<uint32_t>::max();
    }

    auto* swapchain = _render->getSwapchain();
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

void PresentationGraphService::render(float                              deltaTime,
                                      const Extensions&                  extensions,
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

    if (extensions.recordBeforeExtensions) {
        // Contract: this hook runs before the presentation graph is built and
        // recorded, inside the already-open frame command buffer. Content
        // recorded here (e.g. ImGui draw data consumed later by the graph) must
        // not recreate GPU resources; layout transitions must go through the
        // shared resource state tracker.
        extensions.recordBeforeExtensions(cmdBuf);
    }

    const Extent2D presentationExtent = presentationImage->getExtent();
    auto           sourceImage        = _viewportDisplayImageProvider ? _viewportDisplayImageProvider() : nullptr;
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
                    .bOutputIsSRGB  = EFormat::isSRGB(_render->getSwapchain()->getFormat()),
                    .state          = &_presentationPostProcessState,
                });
            }

            if (extensions.recordExtensions) {
                extensions.recordExtensions(&rgCtx.getCommandBuffer());
            }

            rgCtx.endRendering();
        });

    if (extensions.appendCapture) {
        extensions.appendCapture(graph, output, presentationExtent);
    }

    [[maybe_unused]] const bool bExecuted = presentationExecutor->execute(graph, *cmdBuf);
}

} // namespace ya