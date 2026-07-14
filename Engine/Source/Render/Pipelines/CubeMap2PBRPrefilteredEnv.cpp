#include "CubeMap2PBRPrefilteredEnv.h"
#include "Render/Core/RenderGraphExecutor.h"
#include "Render/Core/RenderGraphImportUtils.h"
#include "Render/Core/RenderResourceFactory.h"

#include "Core/Math/Math.h"
#include "Render/Render.h"
#include "Resource/DeferredDeletionQueue.h"
#include "Resource/Mesh/PrimitiveMeshCache.h"

#include <algorithm>
#include <format>

namespace ya
{

namespace
{


DescriptorPoolCreateInfo makePrefilterDescriptorPoolCreateInfo()
{
    return DescriptorPoolCreateInfo{
        .label     = "CubeMap2PBRPrefilterEnv_DSP",
        .maxSets   = 1,
        .poolSizes = {
            DescriptorPoolSize{
                .type            = EPipelineDescriptorType::CombinedImageSampler,
                .descriptorCount = 1,
            },
        },
    };
}

glm::mat4 buildCaptureView(uint32_t faceIndex)
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

glm::mat4 buildCaptureProjection()
{
    return FMath::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
}

} // namespace

void CubeMap2PBRPrefilteredEnv::init(IRender* render)
{

    if (_render == render && _pipelineLayout && _inputSampler) {
        return;
    }

    shutdown();
    _render = render;
    if (!_render) {
        return;
    }


    const auto descriptorSetLayouts = IDescriptorSetLayout::create(
        _render, _pipelineLayoutDesc.descriptorSetLayouts);
    YA_CORE_ASSERT(!descriptorSetLayouts.empty(), "Failed to create CubeMap2PBRPrefilterEnv descriptor set layout");
    _descriptorSetLayout = descriptorSetLayouts[0];
    _pipelineLayout      = IPipelineLayout::create(_render,
                                                   _pipelineLayoutDesc.label,
                                                   _pipelineLayoutDesc.pushConstants,
                                                   descriptorSetLayouts);

    _pipeline       = IGraphicsPipeline::create(_render);
    _inputSampler   = _render->getResourceFactory()->createSampler(
        SamplerDesc{
            .label        = "CubeMap2PBRPrefilterEnv_InputSampler",
            .addressModeU = ESamplerAddressMode::ClampToEdge,
            .addressModeV = ESamplerAddressMode::ClampToEdge,
            .addressModeW = ESamplerAddressMode::ClampToEdge,
        });
}

void CubeMap2PBRPrefilteredEnv::shutdown()
{
    _pipeline.reset();
    _pipelineLayout.reset();
    _inputSampler.reset();
    _transientFaceViews.clear();
    _descriptorSetLayout.reset();
    _pipelineColorFormat = EFormat::Undefined;
    _render              = nullptr;
}

bool CubeMap2PBRPrefilteredEnv::ensurePipeline(EFormat::T colorFormat)
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
                .label                   = "CubeMap2PBRPrefilterEnv",
                .viewMask                = 0,
                .colorAttachmentFormats  = {colorFormat},
                .depthAttachmentFormat   = EFormat::Undefined,
                .stencilAttachmentFormat = EFormat::Undefined,
            },
            .pipelineLayout = _pipelineLayout.get(),
            .shaderDesc     = ShaderDesc{
                .shaderName        = "Misc/CubeMap2PBRPrefilterEnv.slang",
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
                .defines = {"SAMPLE_COUNT 64"},
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
    YA_CORE_ASSERT(bPipelineOK, "Failed to create CubeMap2PBRPrefilterEnv pipeline");
    if (!bPipelineOK) {
        return false;
    }

    _pipelineColorFormat = colorFormat;
    return true;
}

CubeMap2PBRPrefilteredEnv::ExecuteResult CubeMap2PBRPrefilteredEnv::execute(const ExecuteContext& ctx)
{
    ExecuteResult result{};
    if (!_render || !ctx.cmdBuf || !ctx.input || !ctx.output) {
        return result;
    }

    YA_CORE_ASSERT(ctx.input->getImageView(), "CubeMap2PBRPrefilterEnv input texture must have a valid image view");
    YA_CORE_ASSERT(ctx.output->getImageShared() && ctx.output->getImageView(),
                   "CubeMap2PBRPrefilterEnv output texture must own a valid image and cube view");
    YA_CORE_ASSERT(ctx.output->getImage()->getArrayLayers() >= CubeFace_Count,
                   "CubeMap2PBRPrefilterEnv output must be a 6-layer cubemap image");

    ICommandBuffer::LabelScope labelScope(ctx.cmdBuf, "CubeMap2PBRPrefilterEnv");

    if (!ensurePipeline(ctx.output->getFormat())) {
        return result;
    }

    auto* cubeMesh = PrimitiveMeshCache::get().getMesh(EPrimitiveGeometry::Cube);
    YA_CORE_ASSERT(cubeMesh, "CubeMap2PBRPrefilterEnv requires a cube primitive mesh");
    if (!cubeMesh) {
        return result;
    }

    auto transientDescriptorPool = IDescriptorPool::create(_render, makePrefilterDescriptorPoolCreateInfo());
    YA_CORE_ASSERT(transientDescriptorPool, "Failed to create transient CubeMap2PBRPrefilterEnv descriptor pool");
    if (!transientDescriptorPool) {
        return result;
    }

    std::vector<DescriptorSetHandle> descriptorSets;
    const bool bAllocateOK = transientDescriptorPool->allocateDescriptorSets(_descriptorSetLayout, 1, descriptorSets);
    YA_CORE_ASSERT(bAllocateOK && descriptorSets.size() == 1,
                   "Failed to allocate transient CubeMap2PBRPrefilterEnv descriptor set");
    if (!bAllocateOK || descriptorSets.size() != 1) {
        return result;
    }
    const DescriptorSetHandle descriptorSet = descriptorSets[0];
    result.keepAliveResources.push_back(transientDescriptorPool);

    _render->getDescriptorHelper()->updateDescriptorSets(
        {
            IDescriptorSetHelper::genImageWrite(
                descriptorSet,
                0,
                0,
                EPipelineDescriptorType::CombinedImageSampler,
                {
                    DescriptorImageInfo(ctx.input->getImageView()->getHandle(), _inputSampler->getHandle(), EImageLayout::ShaderReadOnlyOptimal),
                }),
        },
        {});

    const uint32_t        mipLevels = std::max(1u, ctx.output->getImage()->getMipLevels());
    RenderGraph graph;
    const auto importedInput = graph.importTexture(
        makeImportedTextureDesc(*ctx.input, ctx.input->getLabel(), EImageLayout::ShaderReadOnlyOptimal));

    for (uint32_t mip = 0; mip < mipLevels; ++mip) {
        const uint32_t mipWidth  = std::max(1u, ctx.output->getWidth() >> mip);
        const uint32_t mipHeight = std::max(1u, ctx.output->getHeight() >> mip);
        const float    roughness = mipLevels <= 1 ? 0.0f : static_cast<float>(mip) / static_cast<float>(mipLevels - 1);

        for (uint32_t face = 0; face < CubeFace_Count; ++face) {
            const auto faceHandle = graph.importTexture(
                makeImportedSubresourceTextureDesc(
                    ctx.output->getImageShared(),
                    ImageViewCreateInfo{
                        .label          = std::format("{}_Mip_{}_Face_{}", ctx.output->getLabel(), mip, face),
                        .viewType       = EImageViewType::View2D,
                        .aspectFlags    = EImageAspect::Color,
                        .baseMipLevel   = mip,
                        .levelCount     = 1,
                        .baseArrayLayer = face,
                        .layerCount     = 1,
                    },
                    Extent3D{mipWidth, mipHeight, 1},
                    std::format("{}_Mip_{}_Face_{}", ctx.output->getLabel(), mip, face),
                    EImageLayout::ShaderReadOnlyOptimal));
            const auto pushConstant = buildPushConstant(face, roughness);
            graph.addPass(
                std::format("CubeMap2PBRPrefilterEnv_Mip_{}_Face_{}", mip, face),
                [&](RGPassBuilder& pass) {
                    pass.read(importedInput);
                    pass.useColorAttachment(faceHandle);
                },
                [&](RGRenderContext& rgCtx) {
                    rgCtx.beginColorRendering({
                        .color      = faceHandle,
                        .renderArea = Rect2D{
                            .pos    = {0.0f, 0.0f},
                            .extent = {static_cast<float>(mipWidth), static_cast<float>(mipHeight)},
                        },
                        .clearValue  = ctx.clearColor,
                        .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                    });
                    rgCtx.getCommandBuffer().bindPipeline(_pipeline.get());
                    rgCtx.getCommandBuffer().setViewport(0.0f, 0.0f, static_cast<float>(mipWidth), static_cast<float>(mipHeight), 0.0f, 1.0f);
                    rgCtx.getCommandBuffer().setScissor(0, 0, mipWidth, mipHeight);
                    rgCtx.getCommandBuffer().bindDescriptorSets(_pipelineLayout.get(), 0, {descriptorSet});
                    rgCtx.getCommandBuffer().pushConstants(_pipelineLayout.get(),
                                                           EShaderStage::Vertex | EShaderStage::Fragment,
                                                           0,
                                                           sizeof(PushConstant),
                                                           &pushConstant);
                    cubeMesh->draw(&rgCtx.getCommandBuffer());
                    rgCtx.endRendering();
                });
        }
    }

    auto executor   = std::make_shared<RenderGraphExecutor>(*_render->getResourceFactory());
    result.bSuccess = executor->execute(graph, *ctx.cmdBuf);
    if (result.bSuccess) {
        result.keepAliveResources.push_back(std::move(executor));
    }
    return result;
}

CubeMap2PBRPrefilteredEnv::PushConstant CubeMap2PBRPrefilteredEnv::buildPushConstant(uint32_t faceIndex, float roughness)
{
    PushConstant pushConstant{};
    pushConstant.view       = buildCaptureView(faceIndex);
    pushConstant.projection = buildCaptureProjection();
    pushConstant.roughness  = roughness;
    return pushConstant;
}

} // namespace ya
