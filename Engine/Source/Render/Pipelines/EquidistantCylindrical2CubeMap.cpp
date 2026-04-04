#include "EquidistantCylindrical2CubeMap.h"

#include "Render/Render.h"

namespace ya
{

namespace
{
PipelineLayoutDesc makePipelineLayoutDesc()
{
    return PipelineLayoutDesc{
        .label         = "EquidistantCylindrical2CubeMap_PipelineLayout",
        .pushConstants = {
            PushConstantRange{
                .offset     = 0,
                .size       = sizeof(EquidistantCylindrical2CubeMap::PushConstant),
                .stageFlags = EShaderStage::Vertex | EShaderStage::Geometry | EShaderStage::Fragment,
            },
        },
        .descriptorSetLayouts = {
            DescriptorSetLayoutDesc{
                .label    = "EquidistantCylindrical2CubeMap_DSL",
                .set      = 0,
                .bindings = {
                    DescriptorSetLayoutBinding{
                        .binding         = 0,
                        .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
                        .descriptorCount = 1,
                        .stageFlags      = EShaderStage::Fragment,
                    },
                },
            },
        },
    };
}

DescriptorPoolCreateInfo makeDescriptorPoolCreateInfo()
{
    return DescriptorPoolCreateInfo{
        .label     = "EquidistantCylindrical2CubeMap_DSP",
        .maxSets   = 1,
        .poolSizes = {
            DescriptorPoolSize{
                .type            = EPipelineDescriptorType::CombinedImageSampler,
                .descriptorCount = 1,
            },
        },
    };
}
} // namespace

void EquidistantCylindrical2CubeMap::init(IRender* render)
{
    if (_render == render && _pipelineLayout && _descriptorPool && _inputSampler) {
        return;
    }

    shutdown();
    _render = render;
    if (!_render) {
        return;
    }

    _pipelineLayoutDesc = makePipelineLayoutDesc();

    const auto descriptorSetLayouts = IDescriptorSetLayout::create(
        _render, _pipelineLayoutDesc.descriptorSetLayouts);
    YA_CORE_ASSERT(!descriptorSetLayouts.empty(), "Failed to create EquidistantCylindrical2CubeMap descriptor set layout");
    _descriptorSetLayout = descriptorSetLayouts[0];
    _pipelineLayout      = IPipelineLayout::create(_render,
                                              _pipelineLayoutDesc.label,
                                              _pipelineLayoutDesc.pushConstants,
                                              descriptorSetLayouts);

    _pipeline       = IGraphicsPipeline::create(_render);
    _descriptorPool = IDescriptorPool::create(_render, makeDescriptorPoolCreateInfo());
    _inputSampler   = Sampler::create(
        SamplerDesc{
              .label        = "EquidistantCylindrical2CubeMap_InputSampler",
              .addressModeU = ESamplerAddressMode::Repeat,
              .addressModeV = ESamplerAddressMode::ClampToEdge,
              .addressModeW = ESamplerAddressMode::ClampToEdge,
        });

    std::vector<DescriptorSetHandle> descriptorSets;
    const bool                       bAllocateOK = _descriptorPool->allocateDescriptorSets(_descriptorSetLayout, 1, descriptorSets);
    YA_CORE_ASSERT(bAllocateOK && descriptorSets.size() == 1,
                   "Failed to allocate EquidistantCylindrical2CubeMap descriptor set");
    _descriptorSet = descriptorSets[0];
}

void EquidistantCylindrical2CubeMap::shutdown()
{
    _descriptorSet = nullptr;
    _pipeline.reset();
    _pipelineLayout.reset();
    _inputSampler.reset();
    _descriptorPool.reset();
    _descriptorSetLayout.reset();
    _pipelineColorFormat = EFormat::Undefined;
    _render              = nullptr;
}

bool EquidistantCylindrical2CubeMap::ensurePipeline(EFormat::T colorFormat)
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
                .label                   = "EquidistantCylindrical2CubeMap",
                .viewMask                = 0,
                .colorAttachmentFormats  = {colorFormat},
                .depthAttachmentFormat   = EFormat::Undefined,
                .stencilAttachmentFormat = EFormat::Undefined,
            },
            .pipelineLayout = _pipelineLayout.get(),
            .shaderDesc     = ShaderDesc{
                .shaderName        = "Misc/EquidistantCylindrical2CubeMap.slang",
                .bDeriveFromShader = false,
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
    YA_CORE_ASSERT(bPipelineOK, "Failed to create EquidistantCylindrical2CubeMap pipeline");
    if (!bPipelineOK) {
        return false;
    }

    _pipelineColorFormat = colorFormat;
    return true;
}

EquidistantCylindrical2CubeMap::ExecuteResult EquidistantCylindrical2CubeMap::execute(const ExecuteContext& ctx)
{
    ExecuteResult result{};
    if (!_render || !ctx.cmdBuf || !ctx.input || !ctx.output) {
        return result;
    }

    YA_CORE_ASSERT(ctx.input->getImageView(),
                   "EquidistantCylindrical2CubeMap input texture must have a valid image view");
    YA_CORE_ASSERT(ctx.output->getImageShared() && ctx.output->getImageView(),
                   "EquidistantCylindrical2CubeMap output texture must own a valid image and cube view");
    YA_CORE_ASSERT(ctx.output->getImage()->getArrayLayers() >= CubeFace_Count,
                   "EquidistantCylindrical2CubeMap output must be a 6-layer cubemap image");

    if (!ensurePipeline(ctx.output->getFormat())) {
        return result;
    }

    auto* textureFactory = _render->getTextureFactory();

    // iamgeView from output image
    result.transientOutputArrayView = textureFactory->createImageView(
        ctx.output->getImageShared(),
        ImageViewCreateInfo{
            .label          = std::format("{}_RenderArray", ctx.output->getLabel()),
            .viewType       = EImageViewType::View2DArray,
            .aspectFlags    = EImageAspect::Color,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = CubeFace_Count,
        });
    YA_CORE_ASSERT(result.transientOutputArrayView,
                   "Failed to create EquidistantCylindrical2CubeMap output array view");

    auto outputRenderTarget = Texture::wrap(
        ctx.output->getImageShared(),
        result.transientOutputArrayView,
        std::format("{}_RenderArray", ctx.output->getLabel()));

    _render->getDescriptorHelper()->updateDescriptorSets(
        {
            IDescriptorSetHelper::genImageWrite(
                _descriptorSet,
                0,
                0,
                EPipelineDescriptorType::CombinedImageSampler,
                {
                    DescriptorImageInfo(ctx.input->getImageView()->getHandle(), _inputSampler->getHandle(), EImageLayout::ShaderReadOnlyOptimal),
                }),
        },
        {});

    ImageSubresourceRange cubeRange{
        .aspectMask     = EImageAspect::Color,
        .baseMipLevel   = 0,
        .levelCount     = 1,
        .baseArrayLayer = 0,
        .layerCount     = CubeFace_Count,
    };
    ctx.cmdBuf->transitionImageLayoutAuto(ctx.input->getImage(), EImageLayout::ShaderReadOnlyOptimal);
    ctx.cmdBuf->transitionImageLayoutAuto(ctx.output->getImage(), EImageLayout::ColorAttachmentOptimal, &cubeRange);

    const auto extent = ctx.output->getExtent();
    for (uint32_t face = 0; face < CubeFace_Count; ++face) {
        const auto pushConstant = buildPushConstant(face, ctx.bFlipVertical);
        RenderingInfo renderInfo{
            .label      = std::format("EquidistantCylindrical2CubeMap_Face_{}", face),
            .renderArea = Rect2D{
                .pos    = {0.0f, 0.0f},
                .extent = {static_cast<float>(extent.width), static_cast<float>(extent.height)},
            },
            .layerCount       = CubeFace_Count,
            .colorClearValues = {ctx.clearColor},
            .depthClearValue  = ClearValue(1.0f, 0),
            .colorAttachments = {
                RenderingInfo::ImageSpec{
                    .texture = outputRenderTarget.get(),
                    .loadOp  = (face == 0) ? EAttachmentLoadOp::Clear : EAttachmentLoadOp::Load,
                    .storeOp = EAttachmentStoreOp::Store,
                },
            },
        };

        ctx.cmdBuf->beginRendering(renderInfo);
        ctx.cmdBuf->bindPipeline(_pipeline.get());
        ctx.cmdBuf->setViewport(0.0f,
                                0.0f,
                                static_cast<float>(extent.width),
                                static_cast<float>(extent.height),
                                0.0f,
                                1.0f);
        ctx.cmdBuf->setScissor(0, 0, extent.width, extent.height);
        ctx.cmdBuf->bindDescriptorSets(_pipelineLayout.get(), 0, {_descriptorSet});
        ctx.cmdBuf->pushConstants(_pipelineLayout.get(),
                                  EShaderStage::Vertex | EShaderStage::Geometry | EShaderStage::Fragment,
                                  0,
                                  sizeof(PushConstant),
                                  &pushConstant);
        ctx.cmdBuf->draw(3, 1, 0, 0);
        ctx.cmdBuf->endRendering(renderInfo);
    }

    ctx.cmdBuf->transitionImageLayoutAuto(ctx.output->getImage(), EImageLayout::ShaderReadOnlyOptimal, &cubeRange);
    result.bSuccess = true;
    return result;
}

EquidistantCylindrical2CubeMap::PushConstant EquidistantCylindrical2CubeMap::buildPushConstant(uint32_t faceIndex,
                                                                                               bool bFlipVertical)
{
    PushConstant pushConstant{};
    pushConstant.faceIndex = faceIndex;
    pushConstant.flipVertical = bFlipVertical;
    return pushConstant;
}

} // namespace ya