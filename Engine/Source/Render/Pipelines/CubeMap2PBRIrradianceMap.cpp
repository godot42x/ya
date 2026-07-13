#include "CubeMap2PBRIrradianceMap.h"
#include "Render/Core/RenderGraphExecutor.h"
#include "Render/Core/RenderResourceFactory.h"

#include "Core/Math/Math.h"
#include "Render/Render.h"
#include "Resource/DeferredDeletionQueue.h"
#include "Resource/Mesh/PrimitiveMeshCache.h"

namespace ya
{

namespace
{


glm::mat4 buildCubeMap2IrradianceCaptureView(uint32_t faceIndex)
{
    const glm::vec3 origin{0.0f, 0.0f, 0.0f};
    const glm::vec3 down{0.0f, -1.0f, 0.0f};
    const glm::vec3 backward{0.0f, 0.0f, 1.0f};

    switch (static_cast<ECubeFace>(faceIndex)) {
    case CubeFace_PosX:
        return FMath::lookAt(origin, origin + glm::vec3(1.0f, 0.0f, 0.0f), down);
    case CubeFace_NegX:
        return FMath::lookAt(origin, origin + glm::vec3(-1.0f, 0.0f, 0.0f), down);
    case CubeFace_PosY:
        return FMath::lookAt(origin, origin + glm::vec3(0.0f, 1.0f, 0.0f), backward);
    case CubeFace_NegY:
        return FMath::lookAt(origin, origin + glm::vec3(0.0f, -1.0f, 0.0f), -backward);
    case CubeFace_PosZ:
        return FMath::lookAt(origin, origin + glm::vec3(0.0f, 0.0f, 1.0f), down);
    case CubeFace_NegZ:
        return FMath::lookAt(origin, origin + glm::vec3(0.0f, 0.0f, -1.0f), down);
    case CubeFace_Count:
        break;
    }

    return glm::mat4(1.0f);
}

glm::mat4 buildCubeMap2IrradianceCaptureProjection()
{
    return FMath::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
}
} // namespace

void CubeMap2PBRIrradianceMap::init(IRender* render)
{
    if (_render == render && _pipelineLayout && _descriptorPool && _inputSampler) {
        return;
    }

    shutdown();
    _render = render;
    if (!_render) {
        return;
    }


    const auto descriptorSetLayouts = IDescriptorSetLayout::create(
        _render, _pipelineLayoutDesc.descriptorSetLayouts);
    YA_CORE_ASSERT(!descriptorSetLayouts.empty(), "Failed to create CubeMap2PBRIrradianceMap descriptor set layout");
    _descriptorSetLayout = descriptorSetLayouts[0];
    _pipelineLayout      = IPipelineLayout::create(_render,
                                                   _pipelineLayoutDesc.label,
                                                   _pipelineLayoutDesc.pushConstants,
                                                   descriptorSetLayouts);

    _pipeline       = IGraphicsPipeline::create(_render);
    _descriptorPool = IDescriptorPool::create(_render, _dspCI);
    _inputSampler   = _render->getResourceFactory()->createSampler(
        SamplerDesc{
            .label        = "CubeMap2PBRIrradianceMap_InputSampler",
            .addressModeU = ESamplerAddressMode::ClampToEdge,
            .addressModeV = ESamplerAddressMode::ClampToEdge,
            .addressModeW = ESamplerAddressMode::ClampToEdge,
        });

    std::vector<DescriptorSetHandle> descriptorSets;
    const bool                       bAllocateOK = _descriptorPool->allocateDescriptorSets(_descriptorSetLayout, 1, descriptorSets);
    YA_CORE_ASSERT(bAllocateOK && descriptorSets.size() == 1,
                   "Failed to allocate CubeMap2PBRIrradianceMap descriptor set");
    _descriptorSet = descriptorSets[0];
}

void CubeMap2PBRIrradianceMap::shutdown()
{
    _descriptorSet = nullptr;
    _pipeline.reset();
    _pipelineLayout.reset();
    _inputSampler.reset();
    _transientFaceViews.clear();
    _descriptorPool.reset();
    _descriptorSetLayout.reset();
    _pipelineColorFormat = EFormat::Undefined;
    _render              = nullptr;
}

bool CubeMap2PBRIrradianceMap::ensurePipeline(EFormat::T colorFormat)
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
                .label                   = "CubeMap2PBRIrradianceMap",
                .viewMask                = 0,
                .colorAttachmentFormats  = {colorFormat},
                .depthAttachmentFormat   = EFormat::Undefined,
                .stencilAttachmentFormat = EFormat::Undefined,
            },
            .pipelineLayout = _pipelineLayout.get(),
            .shaderDesc     = ShaderDesc{
                .shaderName        = "Misc/CubeMap2PBRIrradianceMap.slang",
                .vertexBufferDescs = {
                    VertexBufferDescription{
                        .slot  = 0,
                        .pitch = sizeof(ya::Vertex),
                    },
                },
                .vertexAttributes = {
                    VertexAttribute{
                        .bufferSlot = 0,
                        .location   = 0,
                        .format     = EVertexAttributeFormat::Float3,
                        .offset     = offsetof(ya::Vertex, position),
                    },
                },
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
    YA_CORE_ASSERT(bPipelineOK, "Failed to create CubeMap2PBRIrradianceMap pipeline");
    if (!bPipelineOK) {
        return false;
    }

    _pipelineColorFormat = colorFormat;
    return true;
}

CubeMap2PBRIrradianceMap::ExecuteResult CubeMap2PBRIrradianceMap::execute(const ExecuteContext& ctx)
{
    ExecuteResult result{};
    if (!_render || !ctx.cmdBuf || !ctx.input || !ctx.output) {
        return result;
    }

    YA_CORE_ASSERT(ctx.input->getImageView(),
                   "CubeMap2PBRIrradianceMap input texture must have a valid image view");
    YA_CORE_ASSERT(ctx.output->getImageShared() && ctx.output->getImageView(),
                   "CubeMap2PBRIrradianceMap output texture must own a valid image and cube view");
    YA_CORE_ASSERT(ctx.output->getImage()->getArrayLayers() >= CubeFace_Count,
                   "CubeMap2PBRIrradianceMap output must be a 6-layer cubemap image");

    ICommandBuffer::LabelScope labelScope(ctx.cmdBuf, "CubeMap2PBRIrradianceMap");

    if (!ensurePipeline(ctx.output->getFormat())) {
        return result;
    }

    auto* cubeMesh = PrimitiveMeshCache::get().getMesh(EPrimitiveGeometry::Cube);
    YA_CORE_ASSERT(cubeMesh, "CubeMap2PBRIrradianceMap requires a cube primitive mesh");
    if (!cubeMesh) {
        return result;
    }

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
    const auto extent           = ctx.output->getExtent();
    RenderGraph graph;
    const auto importedInput = graph.importTexture(RGImportedTextureDesc{
        .desc = RGTextureDesc{
            .label       = ctx.input->getLabel(),
            .format      = ctx.input->getFormat(),
            .extent      = Extent3D{ctx.input->getWidth(), ctx.input->getHeight(), 1},
            .mipLevels   = ctx.input->getImage()->getMipLevels(),
            .arrayLayers = ctx.input->getImage()->getArrayLayers(),
            .usage       = ctx.input->getImage()->getUsage(),
        },
        .importDesc = ImportedImageDesc{
            .label         = ctx.input->getLabel(),
            .nativeHandle  = static_cast<void*>(ctx.input->getImage()->getHandle()),
            .format        = ctx.input->getFormat(),
            .usage         = ctx.input->getImage()->getUsage(),
            .extent        = Extent3D{ctx.input->getWidth(), ctx.input->getHeight(), 1},
            .mipLevels     = ctx.input->getImage()->getMipLevels(),
            .arrayLayers   = ctx.input->getImage()->getArrayLayers(),
            .initialLayout = ctx.input->getImage()->getCompatibilityLayout(),
            .finalLayout   = EImageLayout::ShaderReadOnlyOptimal,
        },
        .image = ctx.input->getImageShared(),
    });

    for (uint32_t face = 0; face < CubeFace_Count; ++face) {
        const auto faceHandle = graph.importTexture(RGImportedTextureDesc{
            .desc = RGTextureDesc{
                .label       = std::format("{}_Face_{}", ctx.output->getLabel(), face),
                .format      = ctx.output->getFormat(),
                .extent      = Extent3D{extent.width, extent.height, 1},
                .mipLevels   = 1,
                .arrayLayers = 1,
                .usage       = ctx.output->getImage()->getUsage(),
            },
            .importDesc = ImportedImageDesc{
                .label         = std::format("{}_Face_{}", ctx.output->getLabel(), face),
                .nativeHandle  = static_cast<void*>(ctx.output->getImage()->getHandle()),
                .format        = ctx.output->getFormat(),
                .usage         = ctx.output->getImage()->getUsage(),
                .extent        = Extent3D{ctx.output->getWidth(), ctx.output->getHeight(), 1},
                .mipLevels     = ctx.output->getImage()->getMipLevels(),
                .arrayLayers   = ctx.output->getImage()->getArrayLayers(),
                .initialLayout = ctx.output->getImage()->getCompatibilityLayout(),
                .finalLayout   = EImageLayout::ShaderReadOnlyOptimal,
            },
            .image = ctx.output->getImageShared(),
            .viewDesc = ImageViewCreateInfo{
                .label          = std::format("{}_Face_{}", ctx.output->getLabel(), face),
                .viewType       = EImageViewType::View2D,
                .aspectFlags    = EImageAspect::Color,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = face,
                .layerCount     = 1,
            },
        });

        const auto pushConstant = buildPushConstant(face);
        graph.addPass(
            std::format("CubeMap2PBRIrradianceMap_Face_{}", face),
            [&](RGPassBuilder& pass) {
                pass.read(importedInput);
                pass.useColorAttachment(faceHandle);
            },
            [&](RGRenderContext& rgCtx) {
                rgCtx.beginColorRendering({
                    .color      = faceHandle,
                    .renderArea = Rect2D{
                        .pos    = {0.0f, 0.0f},
                        .extent = {static_cast<float>(extent.width), static_cast<float>(extent.height)},
                    },
                    .clearValue  = ctx.clearColor,
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                });
                rgCtx.getCommandBuffer().bindPipeline(_pipeline.get());
                rgCtx.getCommandBuffer().setViewport(0.0f,
                                                    0.0f,
                                                    static_cast<float>(extent.width),
                                                    static_cast<float>(extent.height),
                                                    0.0f,
                                                    1.0f);
                rgCtx.getCommandBuffer().setScissor(0, 0, extent.width, extent.height);
                rgCtx.getCommandBuffer().bindDescriptorSets(_pipelineLayout.get(), 0, {_descriptorSet});
                rgCtx.getCommandBuffer().pushConstants(_pipelineLayout.get(),
                                                       EShaderStage::Vertex | EShaderStage::Fragment,
                                                       0,
                                                       sizeof(PushConstant),
                                                       &pushConstant);
                cubeMesh->draw(&rgCtx.getCommandBuffer());
                rgCtx.endRendering();
            });
    }

    RenderGraphExecutor executor(*_render->getResourceFactory());
    result.bSuccess = executor.execute(graph, *ctx.cmdBuf);
    return result;
}

CubeMap2PBRIrradianceMap::PushConstant CubeMap2PBRIrradianceMap::buildPushConstant(uint32_t faceIndex)
{
    PushConstant pushConstant{};
    pushConstant.view       = buildCubeMap2IrradianceCaptureView(faceIndex);
    pushConstant.projection = buildCubeMap2IrradianceCaptureProjection();
    pushConstant.faceIndex  = faceIndex;
    return pushConstant;
}

} // namespace ya
