#include "Runtime/Rendering/Common/PostProcessingStage.h"

#include "Render/Core/CommandBuffer.h"
#include "Render/Core/RenderGraphExecutor.h"
#include "Render/Core/RenderGraphImportUtils.h"
#include "Render/Core/Swapchain.h"
#include <algorithm>

namespace ya
{

namespace
{

constexpr std::string_view kPostprocessingOutputExportName = "Postprocessing.Output";

stdptr<RenderImage> createPostprocessRenderImage(IRender* render,
                                                 std::string_view label,
                                                 Extent2D extent,
                                                 EFormat::T format)
{
    if (!render || extent.width == 0 || extent.height == 0) {
        return nullptr;
    }

    return createRenderImage(
        *render->getResourceFactory(),
        RenderImageDesc{
            .image = ImageCreateInfo{
                .label         = std::string(label),
                .format        = format,
                .extent        = {.width = extent.width, .height = extent.height, .depth = 1},
                .mipLevels     = 1,
                .arrayLayers   = 1,
                .samples       = ESampleCount::Sample_1,
                .usage         = EImageUsage::ColorAttachment | EImageUsage::Sampled | EImageUsage::TransferSrc,
                .initialLayout = EImageLayout::Undefined,
            },
            .defaultView = ImageViewCreateInfo{
                .label       = std::string(label) + "_DefaultView",
                .aspectFlags = EImageAspect::Color,
            },
        });
}

RGImportedTextureDesc makePostprocessImportedTextureDesc(const Texture& texture,
                                                         std::string_view label,
                                                         EImageLayout::T finalLayout)
{
    return makeImportedTextureDesc(texture, label, finalLayout);
}

RGImportedTextureDesc makePostprocessImportedTextureDesc(const RenderImage& image,
                                                         std::string_view   label,
                                                         EImageLayout::T    finalLayout)
{
    return makeImportedTextureDesc(image, label, finalLayout);
}

} // namespace

void PostProcessingStage::init(const InitDesc& desc)
{
    _render      = desc.render;
    _colorFormat = desc.colorFormat;
    _graphExecutor = std::make_unique<RenderGraphExecutor>(*_render->getResourceFactory());

    _bloomProcessor = ya::makeShared<BloomPostprocessing>();
    _bloomProcessor->init(BloomPostprocessing::InitDesc{
        .render = _render,
        .pipelineRenderingInfo = PipelineRenderingInfo{
            .label = "BloomPostprocessing",
            .viewMask = 0,
            .colorAttachmentFormats = {BloomPostprocessing::BLOOM_FORMAT},
            .depthAttachmentFormat = EFormat::Undefined,
            .stencilAttachmentFormat = EFormat::Undefined,
        },
    });

    _postProcessor = ya::makeShared<BasicPostprocessing>();
    _postProcessor->init(BasicPostprocessing::InitDesc{
        .render                = _render,
        .renderPass            = nullptr,
        .pipelineRenderingInfo = PipelineRenderingInfo{
            .label                   = "BasicPostprocessing",
            .viewMask                = 0,
            .colorAttachmentFormats  = {desc.colorFormat},
            .depthAttachmentFormat   = EFormat::Undefined,
            .stencilAttachmentFormat = EFormat::Undefined,
        },
    });
}

void PostProcessingStage::shutdown()
{
    _graphExecutor.reset();
    if (_bloomProcessor) {
        _bloomProcessor->shutdown();
        _bloomProcessor.reset();
    }
    if (_postProcessor) {
        _postProcessor->shutdown();
        _postProcessor.reset();
    }

    clearPreparedResources();
    _render = nullptr;
}

void PostProcessingStage::beginFrame()
{
    if (_bloomProcessor) {
        _bloomProcessor->beginFrame();
    }
    if (_postProcessor) {
        _postProcessor->beginFrame();
    }
}

void PostProcessingStage::clearPreparedResources()
{
    _preparedOutputImage.reset();
    if (_bloomProcessor) {
        _bloomProcessor->clearPreparedResources();
    }
}

void PostProcessingStage::capturePreparedResources(const RenderGraphExecutionResult& result)
{
    _preparedOutputImage = result.getExportedTextureShared(kPostprocessingOutputExportName);
    if (_bloomProcessor) {
        _bloomProcessor->capturePreparedResources(result);
    }
}

RGTextureHandle PostProcessingStage::appendGraphPasses(RenderGraph& graph,
                                                       Texture*     inputTexture,
                                                       glm::vec2    viewportExtent,
                                                       FrameContext* ctx)
{
    (void)viewportExtent;
    if (!inputTexture || !inputTexture->isValid()) {
        clearPreparedResources();
        return {};
    }

    const Extent2D inputExtent = inputTexture->getExtent();
    if (inputExtent.width == 0 || inputExtent.height == 0) {
        clearPreparedResources();
        return {};
    }

    const auto input = graph.importTexture(makePostprocessImportedTextureDesc(*inputTexture, "Postprocessing.Input", EImageLayout::ShaderReadOnlyOptimal));
    return appendGraphPasses(graph, input, inputExtent, ctx);
}

RGTextureHandle PostProcessingStage::appendGraphPasses(RenderGraph& graph,
                                                       RenderImage* inputImage,
                                                       glm::vec2    viewportExtent,
                                                       FrameContext* ctx)
{
    (void)viewportExtent;

    if (!inputImage || !inputImage->isValid()) {
        clearPreparedResources();
        return {};
    }

    const Extent2D inputExtent = inputImage->getExtent();
    if (inputExtent.width == 0 || inputExtent.height == 0) {
        clearPreparedResources();
        return {};
    }

    const auto input = graph.importTexture(makePostprocessImportedTextureDesc(*inputImage, "Postprocessing.Input", EImageLayout::ShaderReadOnlyOptimal));
    return appendGraphPasses(graph, input, inputExtent, ctx);
}

RGTextureHandle PostProcessingStage::appendGraphPasses(RenderGraph& graph,
                                                       RGTextureHandle input,
                                                       Extent2D        inputExtent,
                                                       FrameContext*   ctx)
{
    if (!bEnabled || !_postProcessor || !input.isValid() || inputExtent.width == 0 || inputExtent.height == 0) {
        clearPreparedResources();
        return {};
    }

    clearPreparedResources();

    const auto compositeInput = appendBloomGraphPasses(graph, input, inputExtent, ctx);
    return appendFinalizeGraphPasses(graph, FinalizePassParams{
                                                .input         = compositeInput.isValid() ? compositeInput : input,
                                                .inputExtent   = inputExtent,
                                                .bOutputIsSRGB = EFormat::isSRGB(_render->getSwapchain()->getFormat()),
                                                .postContext   = ctx,
                                            });
}

RGTextureHandle PostProcessingStage::appendBloomGraphPasses(RenderGraph&   graph,
                                                            RGTextureHandle input,
                                                            Extent2D        inputExtent,
                                                            FrameContext*   ctx)
{
    (void)ctx;
    clearPreparedResources();
    if (!bEnabled || !input.isValid() || inputExtent.width == 0 || inputExtent.height == 0) {
        return {};
    }

    if (_state.bEnableBloom && _bloomProcessor) {
        return _bloomProcessor->appendGraphPasses(graph, BloomPostprocessing::RenderDesc{
            .sceneHandle  = input,
            .renderExtent = inputExtent,
            .state        = &_state,
        });
    }

    return input;
}

RGTextureHandle PostProcessingStage::appendFinalizeGraphPasses(RenderGraph& graph, const FinalizePassParams& params)
{
    if (!bEnabled || !_postProcessor || !params.input.isValid() || params.inputExtent.width == 0 || params.inputExtent.height == 0) {
        return {};
    }

    const auto output = params.output.isValid() ? params.output : graph.createTexture(RGTextureDesc{
        .label  = "Postprocessing.Output",
        .format = _colorFormat,
        .extent = Extent3D{params.inputExtent.width, params.inputExtent.height, 1},
        .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled | EImageUsage::TransferSrc,
    });
    [[maybe_unused]] const auto pass = graph.addPass(
        "Postprocessing",
        [input = params.input, output, inputExtent = params.inputExtent](RGPassBuilder& pass) {
            pass.read(input);
            pass.declareRaster({
                .renderArea  = Rect2D{.pos = {0.0f, 0.0f}, .extent = inputExtent.toVec2()},
                .layerCount  = 1,
                .colors = {{
                    .color       = output,
                    .clearValue  = ClearValue(0.0f, 0.0f, 0.0f, 1.0f),
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                }},
            });
        },
        [this, input = params.input, inputExtent = params.inputExtent, bOutputIsSRGB = params.bOutputIsSRGB, state = &_state, postContext = params.postContext](RGRenderContext& rgCtx) {
            [[maybe_unused]] const auto rasterParams = rgCtx.getRasterPassExecutionParams();
            rgCtx.beginDeclaredRasterRendering();

            const auto* compositeInputImage = rgCtx.resolveTexture(input);
            YA_CORE_ASSERT(compositeInputImage != nullptr && compositeInputImage->getImageView() != nullptr,
                           "Postprocessing failed to resolve input texture {}", input.index);
            _postProcessor->render(BasicPostprocessing::RenderDesc{
                .cmdBuf         = &rgCtx.getCommandBuffer(),
                .ctx            = postContext,
                .inputImageView = compositeInputImage->getImageView(),
                .renderExtent   = inputExtent,
                .bOutputIsSRGB  = bOutputIsSRGB,
                .state          = state,
            });

            rgCtx.endRendering();
        });

    graph.exportTexture(output, std::string(kPostprocessingOutputExportName));
    return output;
}

RenderImage* PostProcessingStage::execute(ICommandBuffer* cmdBuf,
                                          Texture*        inputTexture,
                                          glm::vec2       viewportExtent,
                                          FrameContext*   ctx)
{
    if (!cmdBuf || !inputTexture) {
        return nullptr;
    }

    ICommandBuffer::LabelScope labelScope(cmdBuf, "Postprocessing");
    RenderGraph graph;
    const auto  output = appendGraphPasses(graph, inputTexture, viewportExtent, ctx);
    if (!output.isValid()) {
        return nullptr;
    }

    YA_CORE_ASSERT(_graphExecutor != nullptr, "PostProcessingStage graph executor is not initialized");
    RenderGraphExecutionResult result;
    if (!_graphExecutor->execute(graph, *cmdBuf, nullptr, &result)) {
        clearPreparedResources();
        return nullptr;
    }

    capturePreparedResources(result);
    return output.isValid() ? _preparedOutputImage.get() : nullptr;
}

RenderImage* PostProcessingStage::execute(ICommandBuffer* cmdBuf,
                                          RenderImage*    inputImage,
                                          glm::vec2       viewportExtent,
                                          FrameContext*   ctx)
{
    if (!cmdBuf || !inputImage) {
        return nullptr;
    }

    ICommandBuffer::LabelScope labelScope(cmdBuf, "Postprocessing");
    RenderGraph graph;
    const auto  output = appendGraphPasses(graph, inputImage, viewportExtent, ctx);
    if (!output.isValid()) {
        return nullptr;
    }

    YA_CORE_ASSERT(_graphExecutor != nullptr, "PostProcessingStage graph executor is not initialized");
    RenderGraphExecutionResult result;
    if (!_graphExecutor->execute(graph, *cmdBuf, nullptr, &result)) {
        clearPreparedResources();
        return nullptr;
    }

    capturePreparedResources(result);
    return output.isValid() ? _preparedOutputImage.get() : nullptr;
}
} // namespace ya
