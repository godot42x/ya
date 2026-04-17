#include "PBRGenerateBrdfLUT.h"

#include "Render/Core/CommandBuffer.h"
#include "Render/Render.h"

namespace ya
{

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

    _pipelineLayout = IPipelineLayout::create(_render,
                                              _pipelineLayoutDesc.label,
                                              _pipelineLayoutDesc.pushConstants,
                                              {});
    _pipeline = IGraphicsPipeline::create(_render);
}

void PBRGenerateBrdfLUT::shutdown()
{
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

    YA_CORE_ASSERT(ctx.output->getImageShared() && ctx.output->getImageView(),
                   "PBRGenerateBrdfLUT output texture must own a valid image and image view");
    if (!ctx.output->getImageShared() || !ctx.output->getImageView()) {
        return result;
    }

    if (!ensurePipeline(ctx.output->getFormat())) {
        return result;
    }

    ICommandBuffer::LabelScope labelScope(ctx.cmdBuf, "PBRGenerateBrdfLUT");

    ImageSubresourceRange outputRange{
        .aspectMask     = EImageAspect::Color,
        .baseMipLevel   = 0,
        .levelCount     = 1,
        .baseArrayLayer = 0,
        .layerCount     = 1,
    };
    ctx.cmdBuf->transitionImageLayoutAuto(ctx.output->getImage(), EImageLayout::ColorAttachmentOptimal, &outputRange);

    RenderingInfo renderInfo{
        .label      = "PBRGenerateBrdfLUT",
        .renderArea = Rect2D{
            .pos    = {0.0f, 0.0f},
            .extent = {static_cast<float>(ctx.output->getWidth()), static_cast<float>(ctx.output->getHeight())},
        },
        .layerCount       = 1,
        .colorClearValues = {ctx.clearColor},
        .depthClearValue  = ClearValue(1.0f, 0),
        .colorAttachments = {
            RenderingInfo::ImageSpec{
                .texture = ctx.output,
                .loadOp  = EAttachmentLoadOp::Clear,
                .storeOp = EAttachmentStoreOp::Store,
            },
        },
    };

    ctx.cmdBuf->beginRendering(renderInfo);
    ctx.cmdBuf->bindPipeline(_pipeline.get());
    ctx.cmdBuf->setViewport(0.0f,
                            0.0f,
                            static_cast<float>(ctx.output->getWidth()),
                            static_cast<float>(ctx.output->getHeight()),
                            0.0f,
                            1.0f);
    ctx.cmdBuf->setScissor(0, 0, ctx.output->getWidth(), ctx.output->getHeight());
    ctx.cmdBuf->draw(3, 1, 0, 0);
    ctx.cmdBuf->endRendering(renderInfo);

    ctx.cmdBuf->transitionImageLayoutAuto(ctx.output->getImage(), EImageLayout::ShaderReadOnlyOptimal, &outputRange);
    result.bSuccess = true;
    return result;
}

} // namespace ya