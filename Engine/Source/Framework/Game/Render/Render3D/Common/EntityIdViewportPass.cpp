#include "EntityIdViewportPass.h"

#include "RHI/Core/RenderResourceFactory.h"
#include "RHI/Render.h"
#include "Render3D/RenderFrameData.h"
#include "RHI/RenderDefines.h"

namespace ya
{

namespace
{

constexpr std::array<VertexAttribute, 1> ID_STATIC_ATTRIBUTES = {
    VertexAttribute{
        .bufferSlot = 0,
        .location   = 0,
        .format     = EVertexAttributeFormat::Float3,
        .offset     = offsetof(ya::Vertex, position),
    },
};

constexpr std::array<VertexAttribute, 3> ID_SKINNED_ATTRIBUTES = {
    VertexAttribute{
        .bufferSlot = 0,
        .location   = 0,
        .format     = EVertexAttributeFormat::Float3,
        .offset     = offsetof(ya::Vertex, position),
    },
    VertexAttribute{
        .bufferSlot = 1,
        .location   = 4,
        .format     = EVertexAttributeFormat::Int4,
        .offset     = offsetof(ya::SkeletonMeshVertex, boneIDs),
    },
    VertexAttribute{
        .bufferSlot = 1,
        .location   = 5,
        .format     = EVertexAttributeFormat::Float4,
        .offset     = offsetof(ya::SkeletonMeshVertex, weights),
    },
};

} // namespace

void EntityIdViewportPass::init(IRender* render, EFormat::T colorFormat, EFormat::T depthFormat)
{
    _render = render;

    _descriptorPool = IDescriptorPool::create(
        render,
        DescriptorPoolCreateInfo{
            .maxSets   = 1,
            .poolSizes = {
                DescriptorPoolSize{
                    .type            = EPipelineDescriptorType::UniformBuffer,
                    .descriptorCount = 1,
                },
            },
        });

    _frameDSL = IDescriptorSetLayout::create(render, DescriptorSetLayoutDesc{
                                                         .label    = "EntityId_Frame_DSL",
                                                         .set      = 0,
                                                         .bindings = {{.binding         = 0,
                                                                       .descriptorType  = EPipelineDescriptorType::UniformBuffer,
                                                                       .descriptorCount = 1,
                                                                       .stageFlags      = EShaderStage::Vertex}},
                                                     });
    _skinningDSL = IDescriptorSetLayout::create(
        render,
        DescriptorSetLayoutDesc{
            .label    = "EntityId_Skinning_DSL",
            .set      = 1,
            .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::StorageBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex}},
        });
    _frameDS = _descriptorPool->allocateDescriptorSets(_frameDSL);
    _frameUBO = render->getResourceFactory()->createBuffer(BufferCreateInfo{
        .label       = "EntityId_Frame_UBO",
        .usage       = EBufferUsage::UniformBuffer,
        .size        = sizeof(FrameUBO),
        .memoryUsage = EMemoryUsage::CpuToGpu,
    });

    const auto pushConstantRange = PushConstantRange{
        .offset     = 0,
        .size       = sizeof(PushConstants),
        .stageFlags = EShaderStage::Vertex | EShaderStage::Fragment,
    };

    _pipelineLayout = IPipelineLayout::create(render, "EntityId_PipelineLayout", {pushConstantRange}, {_frameDSL});
    _skinnedPipelineLayout = IPipelineLayout::create(
        render,
        "EntityId_Skinned_PipelineLayout",
        {pushConstantRange},
        {_frameDSL, _skinningDSL});

    const auto buildPipelineCI = [&](const std::string& label,
                                     const std::vector<std::string>& defines,
                                     const std::vector<VertexBufferDescription>& vertexBufferDescs,
                                     const std::vector<VertexAttribute>& attributes,
                                     IPipelineLayout* layout)
    {
        GraphicsPipelineCreateInfo ci{};
        ci.renderPass = nullptr;
        ci.shaderDesc = {
            .sourceMode        = ShaderDesc::ESourceMode::StageFiles,
            .stageFiles        = {
                ShaderDesc::StageFile{.stage = EShaderStage::Vertex, .file = "EntityId.slang", .entryName = "vertMain"},
                ShaderDesc::StageFile{.stage = EShaderStage::Fragment, .file = "EntityId.slang", .entryName = "fragMain"},
            },
            .vertexBufferDescs = vertexBufferDescs,
            .vertexAttributes  = attributes,
            .defines           = defines,
        };
        ci.pipelineRenderingInfo = {
            .label                  = label,
            .viewMask               = 0,
            .colorAttachmentFormats = {colorFormat},
            .depthAttachmentFormat  = depthFormat,
        };
        ci.pipelineLayout     = layout;
        ci.dynamicFeatures    = {EPipelineDynamicFeature::Viewport, EPipelineDynamicFeature::Scissor};
        ci.primitiveType      = EPrimitiveType::TriangleList;
        ci.rasterizationState = {.polygonMode = EPolygonMode::Fill, .cullMode = ECullMode::Back, .frontFace = EFrontFaceType::CounterClockWise};
        ci.depthStencilState  = {
            .bDepthTestEnable  = true,
            .bDepthWriteEnable = false,
            .depthCompareOp    = ECompareOp::LessOrEqual,
        };
        ci.colorBlendState = {.attachments = {{.index          = 0,
                                               .bBlendEnable   = false,
                                               .colorWriteMask = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A}}};
        ci.viewportState   = {.viewports = {Viewport::defaults()}, .scissors = {Scissor::defaults()}};
        return ci;
    };

    _pipeline = IGraphicsPipeline::create(render);
    YA_CORE_ASSERT(_pipeline->recreate(buildPipelineCI("EntityIdPipeline",
                                                        {},
                                                        {VertexBufferDescription{.slot = 0, .pitch = sizeof(ya::Vertex)}},
                                                        {ID_STATIC_ATTRIBUTES.begin(), ID_STATIC_ATTRIBUTES.end()},
                                                        _pipelineLayout.get())),
                   "Failed to create entity-id pipeline");

    // Billboard pipeline: no vertex buffer, the quad is generated from the
    // vertex id in shader space using the same camera-facing math as the
    // BillboardWorld overlay shader.
    _billboardPipelineLayout = IPipelineLayout::create(render, "EntityId_Billboard_PipelineLayout", {pushConstantRange}, {_frameDSL});
    GraphicsPipelineCreateInfo billboardCI{};
    billboardCI.pipelineRenderingInfo = {
        .label                  = "EntityIdBillboardPipeline",
        .viewMask               = 0,
        .colorAttachmentFormats = {colorFormat},
        .depthAttachmentFormat  = depthFormat,
    };
    billboardCI.pipelineLayout = _billboardPipelineLayout.get();
    billboardCI.shaderDesc = {
        .sourceMode = ShaderDesc::ESourceMode::StageFiles,
        .stageFiles = {
            ShaderDesc::StageFile{.stage = EShaderStage::Vertex, .file = "EntityId.slang", .entryName = "billboardVertMain"},
            ShaderDesc::StageFile{.stage = EShaderStage::Fragment, .file = "EntityId.slang", .entryName = "fragMain"},
        },
        .vertexBufferDescs = {},
        .vertexAttributes  = {},
    };
    billboardCI.dynamicFeatures    = {EPipelineDynamicFeature::Viewport, EPipelineDynamicFeature::Scissor};
    billboardCI.primitiveType      = EPrimitiveType::TriangleList;
    billboardCI.rasterizationState = {.polygonMode = EPolygonMode::Fill, .cullMode = ECullMode::None, .frontFace = EFrontFaceType::CounterClockWise};
    billboardCI.depthStencilState  = {
        .bDepthTestEnable  = true,
        .bDepthWriteEnable = false,
        .depthCompareOp    = ECompareOp::LessOrEqual,
    };
    billboardCI.colorBlendState = {.attachments = {{.index          = 0,
                                                    .bBlendEnable   = false,
                                                    .colorWriteMask = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A}}};
    billboardCI.viewportState   = {.viewports = {Viewport::defaults()}, .scissors = {Scissor::defaults()}};
    _billboardPipeline = IGraphicsPipeline::create(render);
    YA_CORE_ASSERT(_billboardPipeline->recreate(billboardCI), "Failed to create entity-id billboard pipeline");

    _skinnedPipeline = IGraphicsPipeline::create(render);
    YA_CORE_ASSERT(_skinnedPipeline->recreate(buildPipelineCI(
                                                   "EntityIdSkinnedPipeline",
                                                   // Layout sets are {0,1} by position: frame UBO, then skinning storage buffer.
                                                   {"ENABLE_SKINNING 1", "SKINNING_SET_INDEX 1"},
                                                   {
                                                       VertexBufferDescription{.slot = 0, .pitch = sizeof(ya::Vertex)},
                                                       VertexBufferDescription{.slot = 1, .pitch = sizeof(ya::SkeletonMeshVertex)},
                                                   },
                                                   {ID_SKINNED_ATTRIBUTES.begin(), ID_SKINNED_ATTRIBUTES.end()},
                                                   _skinnedPipelineLayout.get())),
                   "Failed to create entity-id skinned pipeline");
}

void EntityIdViewportPass::destroy()
{
    _billboardPipeline.reset();
    _billboardPipelineLayout.reset();
    _skinnedPipeline.reset();
    _skinnedPipelineLayout.reset();
    _pipeline.reset();
    _pipelineLayout.reset();
    _frameUBO.reset();
    _frameDS = nullptr;
    _skinningDSL.reset();
    _frameDSL.reset();
    _descriptorPool.reset();
    _render = nullptr;
}

void EntityIdViewportPass::execute(ICommandBuffer*        cmdBuf,
                                   uint32_t               viewportWidth,
                                   uint32_t               viewportHeight,
                                   const glm::mat4&       viewProj,
                                   const glm::mat4&       view,
                                   const RenderFrameData& frameData,
                                   DescriptorSetHandle    skinningDescriptorSet,
                                   const std::vector<EntityIdBillboard>& billboards)
{
    if (!_render || !cmdBuf || viewportWidth == 0 || viewportHeight == 0) {
        return;
    }

    FrameUBO ubo{
        .viewProj = viewProj,
        .view     = view,
    };
    _frameUBO->writeData(&ubo, sizeof(ubo), 0);
    DescriptorBufferInfo bufferInfo(BufferHandle(_frameUBO->getHandle()), 0, static_cast<uint64_t>(sizeof(FrameUBO)));
    _render->getDescriptorHelper()->updateDescriptorSets(
        {IDescriptorSetHelper::genBufferWrite(_frameDS, 0, 0, EPipelineDescriptorType::UniformBuffer, {bufferInfo})},
        {});

    // Match the viewport passes' reverse-Y convention.
    const auto applyViewport = [&](IPipelineLayout* layout)
    {
        cmdBuf->setViewport(0.0f,
                            static_cast<float>(viewportHeight),
                            static_cast<float>(viewportWidth),
                            -static_cast<float>(viewportHeight),
                            0.0f,
                            1.0f);
        cmdBuf->setScissor(0, 0, viewportWidth, viewportHeight);
        (void)layout;
    };

    const auto& buckets = frameData.drawBuckets;

    cmdBuf->bindPipeline(_pipeline.get());
    applyViewport(_pipelineLayout.get());
    cmdBuf->bindDescriptorSets(_pipelineLayout.get(), 0, {_frameDS});
    drawStaticBucket(cmdBuf, buckets.staticMeshes.pbrDrawItems);
    drawStaticBucket(cmdBuf, buckets.staticMeshes.phongDrawItems);
    drawStaticBucket(cmdBuf, buckets.staticMeshes.unlitDrawItems);
    drawStaticBucket(cmdBuf, buckets.staticMeshes.simpleDrawItems);
    drawStaticBucket(cmdBuf, buckets.staticMeshes.fallbackDrawItems);

    if (skinningDescriptorSet) {
        cmdBuf->bindPipeline(_skinnedPipeline.get());
        applyViewport(_skinnedPipelineLayout.get());
        cmdBuf->bindDescriptorSets(_skinnedPipelineLayout.get(), 0, {_frameDS, skinningDescriptorSet});
        drawSkinnedBucket(cmdBuf, buckets.skinnedMeshes.pbrDrawItems);
        drawSkinnedBucket(cmdBuf, buckets.skinnedMeshes.phongDrawItems);
        drawSkinnedBucket(cmdBuf, buckets.skinnedMeshes.unlitDrawItems);
        drawSkinnedBucket(cmdBuf, buckets.skinnedMeshes.simpleDrawItems);
        drawSkinnedBucket(cmdBuf, buckets.skinnedMeshes.fallbackDrawItems);
    }

    if (!billboards.empty()) {
        cmdBuf->bindPipeline(_billboardPipeline.get());
        applyViewport(_billboardPipelineLayout.get());
        cmdBuf->bindDescriptorSets(_billboardPipelineLayout.get(), 0, {_frameDS});
        drawBillboards(cmdBuf, billboards);
    }
}

void EntityIdViewportPass::drawStaticBucket(ICommandBuffer* cmdBuf, const std::vector<RenderDrawItem>& items)
{
    for (const auto& item : items) {
        if (!item.mesh) {
            continue;
        }

        PushConstants pc{.modelMat = item.worldMatrix, .entityId = item.entityId, .skinningPaletteIndex = -1};
        cmdBuf->pushConstants(_pipelineLayout.get(), EShaderStage::Vertex | EShaderStage::Fragment, 0, sizeof(PushConstants), &pc);
        cmdBuf->bindVertexBuffer(0, item.mesh->getVertexBufferMut(), item.mesh->getVertexBufferOffset());
        cmdBuf->bindIndexBuffer(item.mesh->getIndexBufferMut(), item.mesh->getIndexBufferOffset(), false);
        cmdBuf->drawIndexed(item.mesh->getIndexCount(), 1, 0, 0, 0);
    }
}

void EntityIdViewportPass::drawSkinnedBucket(ICommandBuffer* cmdBuf, const std::vector<RenderDrawItem>& items)
{
    for (const auto& item : items) {
        if (!item.mesh) {
            continue;
        }

        PushConstants pc{.modelMat = item.worldMatrix, .entityId = item.entityId, .skinningPaletteIndex = item.skinningPaletteIndex};
        cmdBuf->pushConstants(_skinnedPipelineLayout.get(), EShaderStage::Vertex | EShaderStage::Fragment, 0, sizeof(PushConstants), &pc);
        item.mesh->drawSkinned(cmdBuf);
    }
}

void EntityIdViewportPass::drawBillboards(ICommandBuffer* cmdBuf, const std::vector<EntityIdBillboard>& billboards)
{
    for (const auto& billboard : billboards) {
        if (billboard.entityId == 0) {
            continue;
        }
        PushConstants pc{};
        pc.entityId    = billboard.entityId;
        pc.worldCenter = billboard.worldCenter;
        pc.worldSize   = billboard.worldSize;
        cmdBuf->pushConstants(_billboardPipelineLayout.get(), EShaderStage::Vertex | EShaderStage::Fragment, 0, sizeof(PushConstants), &pc);
        cmdBuf->draw(6, 1, 0, 0);
    }
}

} // namespace ya
