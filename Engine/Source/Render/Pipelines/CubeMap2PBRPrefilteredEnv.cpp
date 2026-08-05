#include "CubeMap2PBRPrefilteredEnv.h"
#include "Render/Core/Graph/RenderGraphExecutor.h"
#include "Render/Core/Graph/RenderGraphImportUtils.h"
#include "Render/Core/RenderResourceFactory.h"
#include "Render/Core/RenderingInfoUtils.h"

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
    if (!_render || !ctx.cmdBuf || (!ctx.inputImage && !ctx.inputTexture) || !ctx.output) {
        return result;
    }

    auto* const inputImage = ctx.inputImage ? ctx.inputImage->getImage() : (ctx.inputTexture ? ctx.inputTexture->getImage() : nullptr);
    auto* const inputView  = ctx.inputImage ? ctx.inputImage->getImageView() : (ctx.inputTexture ? ctx.inputTexture->getImageView() : nullptr);

    YA_CORE_ASSERT(inputView, "CubeMap2PBRPrefilterEnv input cubemap must have a valid image view");
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
                    DescriptorImageInfo(inputView->getHandle(), _inputSampler->getHandle(), EImageLayout::ShaderReadOnlyOptimal),
                }),
        },
        {});

    _transientFaceViews.clear();

    const uint32_t mipLevels = std::max(1u, ctx.output->getImage()->getMipLevels());
    ImageSubresourceRange cubeRange{
        .aspectMask     = EImageAspect::Color,
        .baseMipLevel   = 0,
        .levelCount     = mipLevels,
        .baseArrayLayer = 0,
        .layerCount     = CubeFace_Count,
    };
    ctx.cmdBuf->transitionImageLayoutAuto(inputImage, EImageLayout::ShaderReadOnlyOptimal);
    ctx.cmdBuf->transitionImageLayoutAuto(ctx.output->getImage(), EImageLayout::ColorAttachmentOptimal, &cubeRange);

    auto* const resourceFactory = _render->getResourceFactory();
    bool        bAllFacesSuccess = true;

    for (uint32_t mip = 0; mip < mipLevels; ++mip) {
        const uint32_t mipWidth  = std::max(1u, ctx.output->getWidth() >> mip);
        const uint32_t mipHeight = std::max(1u, ctx.output->getHeight() >> mip);
        const float    roughness = mipLevels <= 1 ? 0.0f : static_cast<float>(mip) / static_cast<float>(mipLevels - 1);

        for (uint32_t face = 0; face < CubeFace_Count; ++face) {
            const auto faceView = resourceFactory->createImageView(
                ctx.output->getImageShared(),
                ImageViewCreateInfo{
                    .label          = std::format("{}_Mip_{}_Face_{}", ctx.output->getLabel(), mip, face),
                    .viewType       = EImageViewType::View2D,
                    .aspectFlags    = EImageAspect::Color,
                    .baseMipLevel   = mip,
                    .levelCount     = 1,
                    .baseArrayLayer = face,
                    .layerCount     = 1,
                });
            YA_CORE_ASSERT(faceView, "Failed to create CubeMap2PBRPrefilterEnv output face view");
            if (!faceView) {
                bAllFacesSuccess = false;
                break;
            }

            const auto faceTexture = Texture::wrap(
                ctx.output->getImageShared(),
                faceView,
                std::format("{}_Mip_{}_Face_{}", ctx.output->getLabel(), mip, face));
            _transientFaceViews.push_back(faceView);
            const auto pushConstant = buildPushConstant(face, roughness);
            auto colorAttachment = makeRenderAttachment(
                faceTexture->getImageView(),
                EAttachmentLoadOp::Clear,
                EAttachmentStoreOp::Store,
                EImageLayout::ColorAttachmentOptimal,
                EImageLayout::ShaderReadOnlyOptimal,
                ctx.clearColor);
            RenderingInfo renderInfo{
                .label       = std::format("CubeMap2PBRPrefilterEnv_Mip_{}_Face_{}", mip, face),
                .attachments = RenderAttachmentSet{
                    .renderArea = Rect2D{
                        .pos    = {0.0f, 0.0f},
                        .extent = {static_cast<float>(mipWidth), static_cast<float>(mipHeight)},
                    },
                    .layerCount = 1,
                    .colors     = {std::move(colorAttachment)},
                },
            };

            ctx.cmdBuf->beginRendering(renderInfo);
            ctx.cmdBuf->bindPipeline(_pipeline.get());
            ctx.cmdBuf->setViewport(0.0f, 0.0f, static_cast<float>(mipWidth), static_cast<float>(mipHeight), 0.0f, 1.0f);
            ctx.cmdBuf->setScissor(0, 0, mipWidth, mipHeight);
            ctx.cmdBuf->bindDescriptorSets(_pipelineLayout.get(), 0, {descriptorSet});
            ctx.cmdBuf->pushConstants(_pipelineLayout.get(),
                                      EShaderStage::Vertex | EShaderStage::Fragment,
                                      0,
                                      sizeof(PushConstant),
                                      &pushConstant);
            cubeMesh->draw(ctx.cmdBuf);
            ctx.cmdBuf->endRendering(renderInfo);
        }

        if (!bAllFacesSuccess) {
            break;
        }
    }

    ctx.cmdBuf->transitionImageLayoutAuto(ctx.output->getImage(), EImageLayout::ShaderReadOnlyOptimal, &cubeRange);
    result.bSuccess = bAllFacesSuccess;
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
