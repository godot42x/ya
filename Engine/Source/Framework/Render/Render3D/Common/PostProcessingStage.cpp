#include "Render3D/Common/PostProcessingStage.h"

#include "Graph/RenderGraphImportUtils.h"
#include "Render3D/Common/ViewGraphName.h"
#include "RHI/Core/Swapchain.h"
#include <algorithm>

namespace ya
{

namespace
{

RGImportedTextureDesc makePostprocessImportedTextureDesc(const std::shared_ptr<ImageResource>& resource,
                                                         std::string_view                    label,
                                                         EImageLayout::T                     finalLayout)
{
    return makeImportedTextureDesc(resource, label, finalLayout);
}

} // namespace

void PostProcessingStage::init(const InitDesc& desc)
{
    _render      = desc.render;
    _colorFormat = desc.colorFormat;

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
    if (_bloomProcessor) {
        _bloomProcessor->shutdown();
        _bloomProcessor.reset();
    }
    if (_postProcessor) {
        _postProcessor->shutdown();
        _postProcessor.reset();
    }

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

RGTextureHandle PostProcessingStage::appendGraphPasses(RenderGraph& graph,
                                                       Texture*     inputTexture,
                                                       glm::vec2    viewExtent,
                                                       FrameContext* ctx)
{
    (void)viewExtent;
    if (!inputTexture || !inputTexture->isValid()) {
        return {};
    }

    const Extent2D inputExtent = inputTexture->getExtent();
    if (inputExtent.width == 0 || inputExtent.height == 0) {
        return {};
    }

    const auto input = graph.importTexture(makePostprocessImportedTextureDesc(inputTexture ? inputTexture->getResourceShared() : nullptr, "Postprocessing.Input", EImageLayout::ShaderReadOnlyOptimal));
    return appendGraphPasses(graph, input, inputExtent, ctx);
}

RGTextureHandle PostProcessingStage::appendGraphPasses(RenderGraph& graph,
                                                       RenderTexture* inputImage,
                                                       glm::vec2      viewExtent,
                                                       FrameContext*  ctx)
{
    (void)viewExtent;

    if (!inputImage || !inputImage->isValid()) {
        return {};
    }

    const Extent2D inputExtent = inputImage->getExtent();
    if (inputExtent.width == 0 || inputExtent.height == 0) {
        return {};
    }

    const auto input = graph.importTexture(makePostprocessImportedTextureDesc(inputImage->getResourceShared(), "Postprocessing.Input", EImageLayout::ShaderReadOnlyOptimal));
    return appendGraphPasses(graph, input, inputExtent, ctx);
}

RGTextureHandle PostProcessingStage::appendGraphPasses(RenderGraph& graph,
                                                       RGTextureHandle input,
                                                       Extent2D        inputExtent,
                                                       FrameContext*   ctx)
{
    if (!_postProcessor || !input.isValid() || inputExtent.width == 0 || inputExtent.height == 0) {
        return {};
    }

    const auto compositeInput = appendBloomGraphPasses(graph, input, inputExtent, ctx);
    // Finalize is unconditional: it is the pass that makes the View's color a
    // display image, so it runs whether or not grading is on.
    return appendFinalizeGraphPasses(graph, FinalizePassParams{
                                                .input         = compositeInput.isValid() ? compositeInput : input,
                                                .inputExtent   = inputExtent,
                                                .bOutputIsSRGB = EFormat::isSRGB(_colorFormat),
                                                .postContext   = ctx,
                                                .viewId        = 0,
                                            });
}

RGTextureHandle PostProcessingStage::appendBloomGraphPasses(RenderGraph&   graph,
                                                            RGTextureHandle input,
                                                            Extent2D        inputExtent,
                                                            FrameContext*   ctx,
                                                            uint64_t        viewId,
                                                            const BloomPassBindings& bloom,
                                                            RGTextureHandle bloomExtract,
                                                            RGTextureHandle bloomBlur,
                                                            RGTextureHandle bloomComposite)
{
    (void)ctx;
    if (!input.isValid() || inputExtent.width == 0 || inputExtent.height == 0) {
        return {};
    }

    // Bloom is grading, and it is the only stage here that is allowed to be
    // skipped: with grading off there is no bloom, so the input passes through
    // as itself rather than as an invalid handle.
    if (bGradingEnabled && _state.bEnableBloom && _bloomProcessor) {
        return _bloomProcessor->appendGraphPasses(graph, BloomPostprocessing::RenderDesc{
            .sceneHandle  = input,
            .renderExtent = inputExtent,
            .state        = &_state,
            .viewId       = viewId,
            .bloom        = bloom,
            .extractHandle   = bloomExtract,
            .blurHandle      = bloomBlur,
            .compositeHandle = bloomComposite,
        });
    }

    return input;
}

RGTextureHandle PostProcessingStage::appendFinalizeGraphPasses(RenderGraph& graph, const FinalizePassParams& params)
{
    if (!_postProcessor || !params.input.isValid() || params.inputExtent.width == 0 || params.inputExtent.height == 0) {
        return {};
    }

    // The state the finalize pass runs with: the configured grading when it is
    // on, and "no look changes, keep the display encoding" when it is off.
    const PostProcessingState grading = bGradingEnabled ? _state : _state.withoutGrading();

    const auto output = params.output.isValid()
        ? params.output
        : graph.createTexture(RGTextureDesc{
              .label  = "Postprocessing.Output",
              .format = _colorFormat,
              .extent = Extent3D{params.inputExtent.width, params.inputExtent.height, 1},
              .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled | EImageUsage::TransferSrc,
          });
    [[maybe_unused]] const auto pass = graph.addPass(
        makeViewGraphName("Postprocessing", params.viewId),
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
        [this, input = params.input, inputExtent = params.inputExtent, bOutputIsSRGB = params.bOutputIsSRGB, grading, postContext = params.postContext, viewId = params.viewId, toneMap = params.toneMap](RGRenderContext& rgCtx) {
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
                .state          = &grading,
                .viewId         = viewId,
                .toneMap        = toneMap,
            });

            rgCtx.endRendering();
        });

    graph.exportTexture(output, makeViewGraphName(kOutputExportName, params.viewId));
    return output;
}

} // namespace ya
