#include "PBRGenerateBrdfLUT.h"

#include "RHI/Core/CommandBuffer.h"
#include "RenderGraph/RenderGraphExecutor.h"
#include "RenderGraph/RenderGraphImportUtils.h"
#include "RHI/Render.h"

namespace ya
{

namespace
{

RGImportedTextureDesc makeBrdfLutImportedTextureDesc(const RenderImage& image)
{
    return makeImportedTextureDesc(image, "PBRGenerateBrdfLUT.Output", EImageLayout::ShaderReadOnlyOptimal);
}

} // namespace

void PBRGenerateBrdfLUT::init(IRender* render)
{
    if (_render == render && _pipelineLayout) {
        return;
    }

    shutdown();
    _render = render;
    if (!_render) {
        return;
    }

    _graphExecutor = std::make_unique<RenderGraphExecutor>(*_render->getResourceFactory());
    _pipelineLayout = IPipelineLayout::create(_render,
                                              _pipelineLayoutDesc.label,
                                              _pipelineLayoutDesc.pushConstants,
                                              {});
    _pipeline = IGraphicsPipeline::create(_render);
}

void PBRGenerateBrdfLUT::shutdown()
{
    if (_graphExecutor) {
        _graphExecutor->clear();
    }
    _graphExecutor.reset();
    _pipeline.reset();
    _pipelineLayout.reset();
    _pipelineColorFormat = EFormat::Undefined;
    _render              = nullptr;
}

bool PBRGenerateBrdfLUT::ensurePipeline(EFormat::T colorFormat)
{
    if (!_render || !_pipeline || !_pipelineLayout) {
        return false;
    }
    if (_pipelineColorFormat == colorFormat) {
        return true;
    }

    const bool bPipelineOK = _pipeline->recreate(
        GraphicsPipelineCreateInfo{
            .renderPass            = nullptr,
            .pipelineRenderingInfo = PipelineRenderingInfo{
                .label                   = "PBRGenerateBrdfLUT",
                .viewMask                = 0,
                .colorAttachmentFormats  = {colorFormat},
                .depthAttachmentFormat   = EFormat::Undefined,
                .stencilAttachmentFormat = EFormat::Undefined,
            },
            .pipelineLayout = _pipelineLayout.get(),
            .shaderDesc     = ShaderDesc{
                .shaderName        = "Misc/pbr_generate_brdf_lut.slang",
                .vertexBufferDescs = {},
                .vertexAttributes  = {},
            },
            .dynamicFeatures = {
                EPipelineDynamicFeature::Viewport,
                EPipelineDynamicFeature::Scissor,
            },
            .primitiveType      = EPrimitiveType::TriangleList,
            .rasterizationState = RasterizationState{
                .polygonMode = EPolygonMode::Fill,
                .cullMode    = ECullMode::None,
                .frontFace   = EFrontFaceType::CounterClockWise,
            },
            .depthStencilState = DepthStencilState{
                .bDepthTestEnable       = false,
                .bDepthWriteEnable      = false,
                .depthCompareOp         = ECompareOp::Always,
                .bDepthBoundsTestEnable = false,
                .bStencilTestEnable     = false,
                .front                  = {},
                .back                   = {},
            },
            .colorBlendState = ColorBlendState{
                .attachments = {
                    ColorBlendAttachmentState{
                        .index          = 0,
                        .bBlendEnable   = false,
                        .colorWriteMask = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A,
                    },
                },
            },
            .viewportState = ViewportState{
                .viewports = {Viewport::defaults()},
                .scissors  = {Scissor::defaults()},
            },
        });
    YA_CORE_ASSERT(bPipelineOK, "Failed to create PBRGenerateBrdfLUT pipeline");
    if (!bPipelineOK) {
        return false;
    }

    _pipelineColorFormat = colorFormat;
    return true;
}

PBRGenerateBrdfLUT::ExecuteResult PBRGenerateBrdfLUT::execute(const ExecuteContext& ctx)
{
    ExecuteResult result{};
    if (!_render || !ctx.cmdBuf || !ctx.output) {
        return result;
    }
    YA_CORE_ASSERT(_graphExecutor != nullptr, "PBRGenerateBrdfLUT graph executor is not initialized");
    YA_CORE_ASSERT(ctx.output->getImageShared() && ctx.output->getImageView(),
                   "PBRGenerateBrdfLUT output texture must own a valid image and image view");
    if (!ctx.output->getImageShared() || !ctx.output->getImageView()) {
        return result;
    }

    if (!ensurePipeline(ctx.output->getFormat())) {
        return result;
    }

    ICommandBuffer::LabelScope labelScope(ctx.cmdBuf, "PBRGenerateBrdfLUT");

    RenderGraph graph;
    const auto  output = graph.importTexture(makeBrdfLutImportedTextureDesc(*ctx.output));

    graph.addPass(
        "PBRGenerateBrdfLUT",
        [&](RGPassBuilder& pass) {
            pass.declareRaster({
                .renderArea = Rect2D{
                    .pos    = {0.0f, 0.0f},
                    .extent = {static_cast<float>(ctx.output->getWidth()), static_cast<float>(ctx.output->getHeight())},
                },
                .layerCount = 1,
                .colors = {{
                    .color       = output,
                    .clearValue  = ctx.clearColor,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                }},
            });
        },
        [&](RGRenderContext& rgCtx) {
            const auto rasterParams = rgCtx.getRasterPassExecutionParams();
            const auto renderExtent = rasterParams.getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();
            rgCtx.getCommandBuffer().bindPipeline(_pipeline.get());
            rgCtx.getCommandBuffer().setViewport(0.0f,
                                                0.0f,
                                                static_cast<float>(renderExtent.width),
                                                static_cast<float>(renderExtent.height),
                                                0.0f,
                                                1.0f);
            rgCtx.getCommandBuffer().setScissor(0, 0, renderExtent.width, renderExtent.height);
            rgCtx.getCommandBuffer().draw(3, 1, 0, 0);
            rgCtx.endRendering();
        });

    result.bSuccess = _graphExecutor->execute(graph, *ctx.cmdBuf);
    return result;
}

} // namespace ya
