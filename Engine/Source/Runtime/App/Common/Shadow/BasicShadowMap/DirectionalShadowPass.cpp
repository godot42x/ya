#include "DirectionalShadowPass.h"

#include "Render/Core/CommandBuffer.h"
#include "Render/Mesh.h"
#include "Render/RenderFrameData.h"
#include "Render/Render.h"

#include <format>

namespace ya
{

// ═══════════════════════════════════════════════════════════════════════════
// Init / Destroy
// ═══════════════════════════════════════════════════════════════════════════

void DirectionalShadowPass::init(IRender* render, Extent2D shadowExtent)
{
    _render       = render;
    _shadowExtent = shadowExtent;

    // Frame UBO descriptor set layout (set 0: one UBO binding)
    _frameDSL = IDescriptorSetLayout::create(
        _render,
        DescriptorSetLayoutDesc{
            .label    = "DirectionalShadow_Frame_DSL",
            .set      = 0,
            .bindings = {
                {.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex | EShaderStage::Fragment},
            },
        });

    // Skinning SSBO descriptor set layout (set 1)
    _skinningDSL = IDescriptorSetLayout::create(
        _render,
        DescriptorSetLayoutDesc{
            .label    = "DirectionalShadow_Skinning_DSL",
            .set      = 1,
            .bindings = {
                {.binding = 0, .descriptorType = EPipelineDescriptorType::StorageBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex},
            },
        });

    // Pipeline create info
    _pipelineCI = GraphicsPipelineCreateInfo{
        .pipelineRenderingInfo = {
            .label                 = "Directional Shadow Map",
            .depthAttachmentFormat = EFormat::D32_SFLOAT,
        },
        .shaderDesc = ShaderDesc{
            .shaderName = "CombineShadowMappingGenerate.slang",
        },
        .dynamicFeatures    = {EPipelineDynamicFeature::Viewport, EPipelineDynamicFeature::Scissor},
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = {.polygonMode = EPolygonMode::Fill, .cullMode = ECullMode::Front, .frontFace = EFrontFaceType::CounterClockWise},
        .depthStencilState  = {.bDepthTestEnable = true, .bDepthWriteEnable = true, .depthCompareOp = ECompareOp::LessOrEqual},
        .colorBlendState    = {.attachments = {}},
        .viewportState      = {.viewports = {Viewport::defaults()}, .scissors = {Scissor::defaults()}},
    };

    // Static pipeline
    _staticVariant.pipelineLayout = IPipelineLayout::create(
        _render, "DirectionalShadow_Static_PPL",
        {PushConstantRange{.offset = 0, .size = sizeof(ModelPushConstant), .stageFlags = EShaderStage::Vertex}},
        {_frameDSL});

    auto staticCI                    = _pipelineCI;
    staticCI.pipelineLayout          = _staticVariant.pipelineLayout.get();
    staticCI.shaderDesc.vertexBufferDescs = {
        VertexBufferDescription{.slot = 0, .pitch = sizeof(ya::Vertex)},
    };
    staticCI.shaderDesc.vertexAttributes = {
        {.bufferSlot = 0, .location = 0, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, position)},
    };
    _staticVariant.pipeline = IGraphicsPipeline::create(_render);
    YA_CORE_ASSERT(_staticVariant.pipeline && _staticVariant.pipeline->recreate(staticCI),
                   "Failed to create directional shadow static pipeline");

    // Skinned pipeline
    _skinnedVariant.pipelineLayout = IPipelineLayout::create(
        _render, "DirectionalShadow_Skinned_PPL",
        {PushConstantRange{.offset = 0, .size = sizeof(ModelPushConstant), .stageFlags = EShaderStage::Vertex}},
        {_frameDSL, _skinningDSL});

    auto skinnedCI                    = _pipelineCI;
    skinnedCI.pipelineLayout          = _skinnedVariant.pipelineLayout.get();
    skinnedCI.shaderDesc.vertexBufferDescs = {
        VertexBufferDescription{.slot = 0, .pitch = sizeof(ya::Vertex)},
        VertexBufferDescription{.slot = 1, .pitch = sizeof(ya::SkeletonMeshVertex)},
    };
    skinnedCI.shaderDesc.vertexAttributes = {
        {.bufferSlot = 0, .location = 0, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, position)},
        {.bufferSlot = 1, .location = 4, .format = EVertexAttributeFormat::Int4,   .offset = offsetof(ya::SkeletonMeshVertex, boneIDs)},
        {.bufferSlot = 1, .location = 5, .format = EVertexAttributeFormat::Float4, .offset = offsetof(ya::SkeletonMeshVertex, weights)},
    };
    skinnedCI.shaderDesc.defines = {"ENABLE_SKINNING 1", "SKINNING_SET_INDEX 1"};
    _skinnedVariant.pipeline = IGraphicsPipeline::create(_render);
    YA_CORE_ASSERT(_skinnedVariant.pipeline && _skinnedVariant.pipeline->recreate(skinnedCI),
                   "Failed to create directional shadow skinned pipeline");

    // Descriptor pool
    _dsp = IDescriptorPool::create(_render, DescriptorPoolCreateInfo{
        .label     = "DirectionalShadow_DSP",
        .maxSets   = MAX_FLIGHTS_IN_FLIGHT * 2, // 1 frame DS + 1 skinning DS per flight
        .poolSizes = {
            {.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = MAX_FLIGHTS_IN_FLIGHT},
            {.type = EPipelineDescriptorType::StorageBuffer, .descriptorCount = MAX_FLIGHTS_IN_FLIGHT},
        },
    });

    // Allocate per-flight resources
    FrameUBO initialData{};
    for (uint32_t i = 0; i < MAX_FLIGHTS_IN_FLIGHT; ++i) {
        auto& flight = _perFlight[i];
        flight.frameUBO = IBuffer::create(_render, BufferCreateInfo{
            .label       = std::format("DirectionalShadow_Frame_UBO_{}", i),
            .usage       = EBufferUsage::UniformBuffer,
            .size        = sizeof(FrameUBO),
            .memoryUsage = EMemoryUsage::CpuToGpu,
        });
        flight.frameUBO->writeData(&initialData, sizeof(FrameUBO), 0);

        flight.frameDS = _dsp->allocateDescriptorSets(_frameDSL);
        _render->getDescriptorHelper()->updateDescriptorSets({
            IDescriptorSetHelper::writeOneUniformBuffer(flight.frameDS, 0, flight.frameUBO.get()),
        });
    }
}

void DirectionalShadowPass::destroy()
{
    for (auto& flight : _perFlight) {
        flight.frameUBO.reset();
        flight.skinningSSBO.reset();
        flight.frameDS    = nullptr;
        flight.skinningDS = nullptr;
    }
    _dsp.reset();
    _depthTexture.reset();
    _depthView.reset();
    _staticVariant  = {};
    _skinnedVariant = {};
    _skinningDSL.reset();
    _frameDSL.reset();
    _skinningCapacity = 0;
    _render = nullptr;
}

// ═══════════════════════════════════════════════════════════════════════════
// Prepare
// ═══════════════════════════════════════════════════════════════════════════

void DirectionalShadowPass::prepare(uint32_t               flightIndex,
                                     const RenderFrameData& frameData,
                                     const glm::mat4&       directionalLightMatrix)
{
    auto& flight = _perFlight[flightIndex];

    // Write frame UBO with directional light matrix
    FrameUBO uboData{
        .directionalLightMatrix = directionalLightMatrix,
        .numPointLights         = 0,
        .hasDirectionalLight    = 1u,
    };
    flight.frameUBO->writeData(&uboData, sizeof(FrameUBO), 0);

    // Skinning
    const auto& palettes = frameData.skinningPalettes;
    ensureSkinningCapacity(static_cast<uint32_t>(palettes.size()));
    if (!palettes.empty()) {
        flight.skinningSSBO->writeData(palettes.data(), palettes.size() * sizeof(RenderSkinningPalette), 0);
        flight.skinningSSBO->flush();
    }

    // Begin frame for pipelines
    if (_staticVariant.pipeline)  _staticVariant.pipeline->beginFrame();
    if (_skinnedVariant.pipeline) _skinnedVariant.pipeline->beginFrame();
}

// ═══════════════════════════════════════════════════════════════════════════
// Execute
// ═══════════════════════════════════════════════════════════════════════════

void DirectionalShadowPass::execute(ICommandBuffer*        cmdBuf,
                                     uint32_t               flightIndex,
                                     const RenderFrameData& frameData)
{
    if (!_depthTexture) return;

    RenderingInfo::ImageSpec depthSpec{
        .texture       = _depthTexture.get(),
        .loadOp        = EAttachmentLoadOp::Clear,
        .storeOp       = EAttachmentStoreOp::Store,
        .initialLayout = EImageLayout::DepthStencilAttachmentOptimal,
        .finalLayout   = EImageLayout::ShaderReadOnlyOptimal,
    };
    RenderingInfo renderInfo{
        .label           = "DirectionalShadowPass",
        .renderArea      = Rect2D{.pos = {0.0f, 0.0f}, .extent = _shadowExtent.toVec2()},
        .layerCount      = 1,
        .depthClearValue = ClearValue(1.0f, 0),
        .depthAttachment = &depthSpec,
    };

    cmdBuf->beginRendering(renderInfo);
    cmdBuf->setViewport(0.0f, 0.0f, static_cast<float>(_shadowExtent.width),
                         static_cast<float>(_shadowExtent.height), 0.0f, 1.0f);
    cmdBuf->setScissor(0, 0, _shadowExtent.width, _shadowExtent.height);

    drawStaticBuckets(cmdBuf, flightIndex, frameData.drawBuckets.staticMeshes);
    drawSkinnedBuckets(cmdBuf, flightIndex, frameData.drawBuckets.skinnedMeshes);

    cmdBuf->endRendering(renderInfo);
}

// ═══════════════════════════════════════════════════════════════════════════
// Draw helpers
// ═══════════════════════════════════════════════════════════════════════════

void DirectionalShadowPass::drawStaticBuckets(ICommandBuffer* cmdBuf, uint32_t flightIndex,
                                               const RenderShadingDrawBuckets& buckets) const
{
    auto drawItems = [&](const std::vector<RenderDrawItem>& items)
    {
        if (items.empty()) return;
        const auto& flight = _perFlight[flightIndex];
        cmdBuf->bindPipeline(_staticVariant.pipeline.get());
        cmdBuf->bindDescriptorSets(_staticVariant.pipelineLayout.get(), 0, {flight.frameDS});
        for (const auto& item : items) {
            if (!item.mesh) continue;
            ModelPushConstant pc{.modelMat = item.worldMatrix, .skinningPaletteIndex = -1};
            cmdBuf->pushConstants(_staticVariant.pipelineLayout.get(), EShaderStage::Vertex, 0, sizeof(ModelPushConstant), &pc);
            item.mesh->drawStatic(cmdBuf);
        }
    };

    drawItems(buckets.pbrDrawItems);
    drawItems(buckets.phongDrawItems);
    drawItems(buckets.unlitDrawItems);
    drawItems(buckets.simpleDrawItems);
    drawItems(buckets.fallbackDrawItems);
}

void DirectionalShadowPass::drawSkinnedBuckets(ICommandBuffer* cmdBuf, uint32_t flightIndex,
                                                const RenderShadingDrawBuckets& buckets) const
{
    auto drawItems = [&](const std::vector<RenderDrawItem>& items)
    {
        if (items.empty()) return;
        const auto& flight = _perFlight[flightIndex];
        cmdBuf->bindPipeline(_skinnedVariant.pipeline.get());
        cmdBuf->bindDescriptorSets(_skinnedVariant.pipelineLayout.get(), 0, {flight.frameDS, flight.skinningDS});
        for (const auto& item : items) {
            if (!item.mesh) continue;
            ModelPushConstant pc{.modelMat = item.worldMatrix, .skinningPaletteIndex = item.skinningPaletteIndex};
            cmdBuf->pushConstants(_skinnedVariant.pipelineLayout.get(), EShaderStage::Vertex, 0, sizeof(ModelPushConstant), &pc);
            item.mesh->drawSkinned(cmdBuf);
        }
    };

    drawItems(buckets.pbrDrawItems);
    drawItems(buckets.phongDrawItems);
    drawItems(buckets.unlitDrawItems);
    drawItems(buckets.simpleDrawItems);
    drawItems(buckets.fallbackDrawItems);
}

// ═══════════════════════════════════════════════════════════════════════════
// Skinning capacity
// ═══════════════════════════════════════════════════════════════════════════

void DirectionalShadowPass::ensureSkinningCapacity(uint32_t paletteCount)
{
    const uint32_t required = std::max(1u, paletteCount);
    if (_dsp && required <= _skinningCapacity && _perFlight[0].skinningDS) return;

    uint32_t newCap = _skinningCapacity == 0 ? 4u : _skinningCapacity;
    while (newCap < required) newCap *= 2;
    _skinningCapacity = newCap;

    const uint32_t bufferSize = _skinningCapacity * sizeof(RenderSkinningPalette);
    for (uint32_t i = 0; i < MAX_FLIGHTS_IN_FLIGHT; ++i) {
        auto& flight = _perFlight[i];
        flight.skinningSSBO = IBuffer::create(_render, BufferCreateInfo{
            .label       = std::format("DirectionalShadow_Skinning_SSBO_{}", i),
            .usage       = EBufferUsage::StorageBuffer,
            .size        = bufferSize,
            .memoryUsage = EMemoryUsage::CpuToGpu,
        });

        if (!flight.skinningDS) {
            flight.skinningDS = _dsp->allocateDescriptorSets(_skinningDSL);
        }
        _render->getDescriptorHelper()->updateDescriptorSets(
            {IDescriptorSetHelper::writeOneStorageBuffer(flight.skinningDS, 0, flight.skinningSSBO.get())}, {});
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Pipeline refresh
// ═══════════════════════════════════════════════════════════════════════════

void DirectionalShadowPass::refreshPipeline(EFormat::T depthFormat)
{
    _pipelineCI.pipelineRenderingInfo.depthAttachmentFormat = depthFormat;
    if (_staticVariant.pipeline) {
        auto ci           = _pipelineCI;
        ci.pipelineLayout = _staticVariant.pipelineLayout.get();
        ci.shaderDesc.vertexBufferDescs = {
            VertexBufferDescription{.slot = 0, .pitch = sizeof(ya::Vertex)},
        };
        ci.shaderDesc.vertexAttributes = {
            {.bufferSlot = 0, .location = 0, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, position)},
        };
        _staticVariant.pipeline->updateDesc(ci);
    }
    if (_skinnedVariant.pipeline) {
        auto ci           = _pipelineCI;
        ci.pipelineLayout = _skinnedVariant.pipelineLayout.get();
        ci.shaderDesc.vertexBufferDescs = {
            VertexBufferDescription{.slot = 0, .pitch = sizeof(ya::Vertex)},
            VertexBufferDescription{.slot = 1, .pitch = sizeof(ya::SkeletonMeshVertex)},
        };
        ci.shaderDesc.vertexAttributes = {
            {.bufferSlot = 0, .location = 0, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, position)},
            {.bufferSlot = 1, .location = 4, .format = EVertexAttributeFormat::Int4,   .offset = offsetof(ya::SkeletonMeshVertex, boneIDs)},
            {.bufferSlot = 1, .location = 5, .format = EVertexAttributeFormat::Float4, .offset = offsetof(ya::SkeletonMeshVertex, weights)},
        };
        ci.shaderDesc.defines = {"ENABLE_SKINNING 1", "SKINNING_SET_INDEX 1"};
        _skinnedVariant.pipeline->updateDesc(ci);
    }
}

void DirectionalShadowPass::renderGUI()
{
    // Directional shadow has no runtime-tweakable params currently
}

void DirectionalShadowPass::setDepthTexture(stdptr<Texture> texture, stdptr<IImageView> view)
{
    _depthTexture = std::move(texture);
    _depthView    = std::move(view);
}

} // namespace ya
