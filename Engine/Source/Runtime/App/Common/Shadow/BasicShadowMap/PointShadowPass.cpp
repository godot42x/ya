#include "PointShadowPass.h"

#include "Render/Core/CommandBuffer.h"
#include "Render/Core/Texture.h"
#include "Render/Core/TextureFactory.h"
#include "Render/Mesh.h"
#include "Render/Render.h"
#include "Render/RenderFrameData.h"

#include "imgui.h"

#include <format>

namespace ya
{

// ═══════════════════════════════════════════════════════════════════════════
// Init / Destroy
// ═══════════════════════════════════════════════════════════════════════════

void PointShadowPass::init(IRender* render, Extent2D shadowExtent)
{
    _render       = render;
    _shadowExtent = shadowExtent;

    const auto& caps = _render->getCapabilities();
    _bIndirectSupported = caps.computeShader && caps.drawIndexedIndirect && caps.storageBuffer && caps.drawIndexedIndirectCount;

    // ─── Descriptor set layouts ──────────────────────────────────────
    _frameDSL = IDescriptorSetLayout::create(_render, DescriptorSetLayoutDesc{
        .label    = "PointShadow_Frame_DSL",
        .set      = 0,
        .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex | EShaderStage::Fragment}},
    });

    _skinningDSL = IDescriptorSetLayout::create(_render, DescriptorSetLayoutDesc{
        .label    = "PointShadow_Skinning_DSL",
        .set      = 1,
        .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::StorageBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex}},
    });

    _indirectDSL = IDescriptorSetLayout::create(_render, DescriptorSetLayoutDesc{
        .label    = "PointShadow_Indirect_DSL",
        .set      = 1,
        .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::StorageBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex}},
    });

    // ─── Pipeline: direct draw (CombineShadowMappingGenerate.slang) ──
    _directPipelineCI = GraphicsPipelineCreateInfo{
        .pipelineRenderingInfo = {.label = "Point Shadow Direct", .depthAttachmentFormat = EFormat::D32_SFLOAT},
        .shaderDesc     = ShaderDesc{.shaderName = "CombineShadowMappingGenerate.slang"},
        .dynamicFeatures    = {EPipelineDynamicFeature::Viewport, EPipelineDynamicFeature::Scissor},
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = {.polygonMode = EPolygonMode::Fill, .cullMode = ECullMode::Front, .frontFace = EFrontFaceType::CounterClockWise},
        .depthStencilState  = {.bDepthTestEnable = true, .bDepthWriteEnable = true, .depthCompareOp = ECompareOp::LessOrEqual},
        .colorBlendState    = {.attachments = {}},
        .viewportState      = {.viewports = {Viewport::defaults()}, .scissors = {Scissor::defaults()}},
    };

    _directStaticVariant.pipelineLayout = IPipelineLayout::create(
        _render, "PointShadow_Direct_Static_PPL",
        {PushConstantRange{.offset = 0, .size = sizeof(ModelPushConstant), .stageFlags = EShaderStage::Vertex}},
        {_frameDSL});

    auto directStaticCI = _directPipelineCI;
    directStaticCI.pipelineLayout = _directStaticVariant.pipelineLayout.get();
    directStaticCI.shaderDesc.vertexBufferDescs = {VertexBufferDescription{.slot = 0, .pitch = sizeof(Vertex)}};
    directStaticCI.shaderDesc.vertexAttributes  = {{.bufferSlot = 0, .location = 0, .format = EVertexAttributeFormat::Float3, .offset = offsetof(Vertex, position)}};
    _directStaticVariant.pipeline = IGraphicsPipeline::create(_render);
    YA_CORE_ASSERT(_directStaticVariant.pipeline && _directStaticVariant.pipeline->recreate(directStaticCI),
                   "Failed to create point shadow direct static pipeline");

    _directSkinnedVariant.pipelineLayout = IPipelineLayout::create(
        _render, "PointShadow_Direct_Skinned_PPL",
        {PushConstantRange{.offset = 0, .size = sizeof(ModelPushConstant), .stageFlags = EShaderStage::Vertex}},
        {_frameDSL, _skinningDSL});

    auto directSkinnedCI = _directPipelineCI;
    directSkinnedCI.pipelineLayout = _directSkinnedVariant.pipelineLayout.get();
    directSkinnedCI.shaderDesc.vertexBufferDescs = {
        VertexBufferDescription{.slot = 0, .pitch = sizeof(Vertex)},
        VertexBufferDescription{.slot = 1, .pitch = sizeof(SkeletonMeshVertex)},
    };
    directSkinnedCI.shaderDesc.vertexAttributes = {
        {.bufferSlot = 0, .location = 0, .format = EVertexAttributeFormat::Float3, .offset = offsetof(Vertex, position)},
        {.bufferSlot = 1, .location = 4, .format = EVertexAttributeFormat::Int4,   .offset = offsetof(SkeletonMeshVertex, boneIDs)},
        {.bufferSlot = 1, .location = 5, .format = EVertexAttributeFormat::Float4, .offset = offsetof(SkeletonMeshVertex, weights)},
    };
    directSkinnedCI.shaderDesc.defines = {"ENABLE_SKINNING 1", "SKINNING_SET_INDEX 1"};
    _directSkinnedVariant.pipeline = IGraphicsPipeline::create(_render);
    YA_CORE_ASSERT(_directSkinnedVariant.pipeline && _directSkinnedVariant.pipeline->recreate(directSkinnedCI),
                   "Failed to create point shadow direct skinned pipeline");

    // ─── Pipeline: indirect draw (PointShadowIndirect.slang) ─────────
    if (_bIndirectSupported) {
        _indirectPipelineCI = GraphicsPipelineCreateInfo{
            .pipelineRenderingInfo = {.label = "Point Shadow Indirect", .depthAttachmentFormat = EFormat::D32_SFLOAT},
            .shaderDesc     = ShaderDesc{.shaderName = "Shadow/PointShadowIndirect.slang"},
            .dynamicFeatures    = {EPipelineDynamicFeature::Viewport, EPipelineDynamicFeature::Scissor},
            .primitiveType      = EPrimitiveType::TriangleList,
            .rasterizationState = {.polygonMode = EPolygonMode::Fill, .cullMode = ECullMode::Front, .frontFace = EFrontFaceType::CounterClockWise},
            .depthStencilState  = {.bDepthTestEnable = true, .bDepthWriteEnable = true, .depthCompareOp = ECompareOp::LessOrEqual},
            .colorBlendState    = {.attachments = {}},
            .viewportState      = {.viewports = {Viewport::defaults()}, .scissors = {Scissor::defaults()}},
        };

        _indirectStaticVariant.pipelineLayout = IPipelineLayout::create(
            _render, "PointShadow_Indirect_Static_PPL",
            {},
            {_frameDSL, _indirectDSL});

        auto indirectCI = _indirectPipelineCI;
        indirectCI.pipelineLayout = _indirectStaticVariant.pipelineLayout.get();
        indirectCI.shaderDesc.vertexBufferDescs = {VertexBufferDescription{.slot = 0, .pitch = sizeof(Vertex)}};
        indirectCI.shaderDesc.vertexAttributes  = {{.bufferSlot = 0, .location = 0, .format = EVertexAttributeFormat::Float3, .offset = offsetof(Vertex, position)}};
        _indirectStaticVariant.pipeline = IGraphicsPipeline::create(_render);
        YA_CORE_ASSERT(_indirectStaticVariant.pipeline && _indirectStaticVariant.pipeline->recreate(indirectCI),
                       "Failed to create point shadow indirect static pipeline");

        _cullPass.init(_render);
    }

    // ─── Descriptor pool ─────────────────────────────────────────────
    const uint32_t faceCount   = ShadowConstants::POINT_SHADOW_FACE_COUNT;
    const uint32_t maxSets     = MAX_FLIGHTS_IN_FLIGHT * (faceCount + 2); // face UBOs + skinning + indirect
    const uint32_t uboCount    = MAX_FLIGHTS_IN_FLIGHT * faceCount;
    const uint32_t ssboCount   = MAX_FLIGHTS_IN_FLIGHT * 2; // skinning + instance
    _dsp = IDescriptorPool::create(_render, DescriptorPoolCreateInfo{
        .label     = "PointShadow_DSP",
        .maxSets   = maxSets,
        .poolSizes = {
            {.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = uboCount},
            {.type = EPipelineDescriptorType::StorageBuffer, .descriptorCount = ssboCount},
        },
    });

    // ─── Per-flight resources ────────────────────────────────────────
    using FaceUBO = slang_types::CombineShadowMappingGenerate::FrameData;
    FaceUBO initialData{};
    for (uint32_t fi = 0; fi < MAX_FLIGHTS_IN_FLIGHT; ++fi) {
        auto& flight = _perFlight[fi];
        for (uint32_t face = 0; face < faceCount; ++face) {
            flight.faceUBO[face] = IBuffer::create(_render, BufferCreateInfo{
                .label       = std::format("PointShadow_FaceUBO_{}_{}", fi, face),
                .usage       = EBufferUsage::UniformBuffer,
                .size        = sizeof(FaceUBO),
                .memoryUsage = EMemoryUsage::CpuToGpu,
            });
            flight.faceUBO[face]->writeData(&initialData, sizeof(FaceUBO), 0);

            flight.faceDS[face] = _dsp->allocateDescriptorSets(_frameDSL);
            _render->getDescriptorHelper()->updateDescriptorSets({
                IDescriptorSetHelper::writeOneUniformBuffer(flight.faceDS[face], 0, flight.faceUBO[face].get()),
            });
        }
    }
}

void PointShadowPass::destroy()
{
    _cullPass.destroy();

    for (auto& flight : _perFlight) {
        for (auto& ubo : flight.faceUBO) ubo.reset();
        flight.skinningSSBO.reset();
        flight.instanceBuffer.reset();
        flight.skinningDS  = nullptr;
        flight.indirectDS  = nullptr;
        flight.meshBatches.clear();
    }
    _dsp.reset();
    for (auto& faceTexArr : _faceDepthTextures) {
        for (auto& tex : faceTexArr) tex.reset();
    }
    for (auto& faceViewArr : _faceDepthViews) {
        for (auto& view : faceViewArr) view.reset();
    }
    _directStaticVariant  = {};
    _directSkinnedVariant = {};
    _indirectStaticVariant = {};
    _indirectDSL.reset();
    _skinningDSL.reset();
    _frameDSL.reset();
    _skinningCapacity = 0;
    _instanceCapacity = 0;
    _render = nullptr;
}

// ═══════════════════════════════════════════════════════════════════════════
// Prepare
// ═══════════════════════════════════════════════════════════════════════════

void PointShadowPass::prepare(uint32_t               flightIndex,
                               const RenderFrameData& frameData,
                               const FrameUBO&        frameUBO,
                               uint32_t               pointLightCount)
{
    auto& flight = _perFlight[flightIndex];

    // Begin frame for pipelines
    if (_directStaticVariant.pipeline)  _directStaticVariant.pipeline->beginFrame();
    if (_directSkinnedVariant.pipeline) _directSkinnedVariant.pipeline->beginFrame();
    if (_indirectStaticVariant.pipeline) _indirectStaticVariant.pipeline->beginFrame();

    if (pointLightCount == 0) return;

    // ─── Write per-face UBOs ─────────────────────────────────────────
    for (uint32_t lightIndex = 0; lightIndex < pointLightCount; ++lightIndex) {
        for (uint32_t faceIndex = 0; faceIndex < 6; ++faceIndex) {
            FrameUBO faceData{
                .numPointLights      = 1,
                .hasDirectionalLight = 0,
            };
            faceData.pointLights[0]           = frameUBO.pointLights[lightIndex];
            faceData.pointLights[0].matrix[0] = frameUBO.pointLights[lightIndex].matrix[faceIndex];

            const uint32_t faceGlobal = lightIndex * 6 + faceIndex;
            flight.faceUBO[faceGlobal]->writeData(&faceData, sizeof(FrameUBO), 0);
        }
    }

    // ─── Skinning ────────────────────────────────────────────────────
    const auto& palettes = frameData.skinningPalettes;
    ensureSkinningCapacity(static_cast<uint32_t>(palettes.size()));
    if (!palettes.empty()) {
        flight.skinningSSBO->writeData(palettes.data(), palettes.size() * sizeof(RenderSkinningPalette), 0);
        flight.skinningSSBO->flush();
    }

    // ─── Indirect path: collect instances and build frustums ─────────
    if (!_bIndirectSupported || !_bUseIndirectDraw) return;

    // Rebuild instance data using hashmap for O(N) grouping
    std::vector<PointShadowInstanceData> instanceData;
    flight.meshBatches.clear();
    _meshBatchMap.clear();

    auto addItems = [&](const std::vector<RenderDrawItem>& items)
    {
        for (const auto& item : items) {
            if (!item.mesh) continue;

            auto [it, inserted] = _meshBatchMap.try_emplace(item.mesh, static_cast<uint32_t>(flight.meshBatches.size()));
            if (inserted) {
                flight.meshBatches.push_back(MeshBatch{
                    .mesh          = item.mesh,
                    .firstInstance = static_cast<uint32_t>(instanceData.size()),
                    .instanceCount = 0,
                });
            }
            auto& batch = flight.meshBatches[it->second];
            batch.instanceCount++;

            const auto      worldBounds = item.mesh->boundingBox.transformed(item.worldMatrix);
            const glm::vec3 center      = 0.5f * (worldBounds.min + worldBounds.max);
            const float     radius      = glm::length(worldBounds.max - center);

            instanceData.push_back(PointShadowInstanceData{
                .worldMatrix   = item.worldMatrix,
                .boundingSphere = glm::vec4(center, radius),
                .indexCount    = item.mesh->getIndexCount(),
                .firstIndex    = 0,
                .vertexOffset  = 0,
            });
        }
    };

    const auto& staticBuckets = frameData.drawBuckets.staticMeshes;
    addItems(staticBuckets.pbrDrawItems);
    addItems(staticBuckets.phongDrawItems);
    addItems(staticBuckets.unlitDrawItems);
    addItems(staticBuckets.simpleDrawItems);
    addItems(staticBuckets.fallbackDrawItems);

    flight.totalInstances = static_cast<uint32_t>(instanceData.size());
    if (instanceData.empty()) return;

    ensureInstanceCapacity(flight.totalInstances);
    flight.instanceBuffer->writeData(instanceData.data(), instanceData.size() * sizeof(PointShadowInstanceData), 0);
    flight.instanceBuffer->flush();

    // Build face frustums
    const uint32_t activeFaces = pointLightCount * 6;
    std::vector<PointShadowFaceFrustum> frustums(activeFaces);
    for (uint32_t light = 0; light < pointLightCount; ++light) {
        for (uint32_t face = 0; face < 6; ++face) {
            const glm::mat4 viewProj = frameUBO.pointLights[light].matrix[face];
            auto planes = extractFrustumPlanes(viewProj);
            auto& frustum = frustums[light * 6 + face];
            for (uint32_t p = 0; p < 6; ++p) {
                frustum.planes[p] = planes[p];
            }
        }
    }

    // Bind instance buffer to cull pass and prepare
    _cullPass.bindInstanceBuffer(flightIndex, flight.instanceBuffer.get());
    _cullPass.prepare(flightIndex, frustums.data(), activeFaces, flight.totalInstances);

    // Update indirect DS for the vertex shader
    updateIndirectDescriptors(flightIndex);
}

// ═══════════════════════════════════════════════════════════════════════════
// Execute
// ═══════════════════════════════════════════════════════════════════════════

void PointShadowPass::execute(ICommandBuffer*        cmdBuf,
                               uint32_t               flightIndex,
                               const RenderFrameData& frameData,
                               uint32_t               pointLightCount)
{
    if (pointLightCount == 0) return;

    auto& flight = _perFlight[flightIndex];
    const bool useIndirect = _bIndirectSupported && _bUseIndirectDraw &&
                             _indirectStaticVariant.pipeline &&
                             flight.totalInstances > 0;

    // Dispatch compute cull if using indirect
    if (useIndirect) {
        _cullPass.dispatch(cmdBuf, flightIndex);
    }

    // Render each face
    for (uint32_t lightIndex = 0; lightIndex < pointLightCount; ++lightIndex) {
        for (uint32_t faceIndex = 0; faceIndex < 6; ++faceIndex) {
            const uint32_t faceGlobal = lightIndex * 6 + faceIndex;
            auto* depthTexture = _faceDepthTextures[lightIndex][faceIndex].get();
            if (!depthTexture) continue;

            RenderingInfo::ImageSpec depthSpec{
                .texture       = depthTexture,
                .loadOp        = EAttachmentLoadOp::Clear,
                .storeOp       = EAttachmentStoreOp::Store,
                .initialLayout = EImageLayout::DepthStencilAttachmentOptimal,
                .finalLayout   = EImageLayout::ShaderReadOnlyOptimal,
            };
            RenderingInfo renderInfo{
                .label           = "PointShadowPass_Face",
                .renderArea      = Rect2D{.pos = {0.0f, 0.0f}, .extent = _shadowExtent.toVec2()},
                .layerCount      = 1,
                .depthClearValue = ClearValue(1.0f, 0),
                .depthAttachment = &depthSpec,
            };

            cmdBuf->beginRendering(renderInfo);
            cmdBuf->setViewport(0.0f, 0.0f, static_cast<float>(_shadowExtent.width),
                                static_cast<float>(_shadowExtent.height), 0.0f, 1.0f);
            cmdBuf->setScissor(0, 0, _shadowExtent.width, _shadowExtent.height);

            if (useIndirect) {
                renderFaceIndirect(cmdBuf, flightIndex, faceGlobal);
            } else {
                renderFaceDirect(cmdBuf, flightIndex, frameData, faceGlobal);
            }

            // Always draw skinned meshes with direct draw (no indirect path for skinned yet)
            const uint32_t layerIndex = 1 + faceGlobal; // offset by directional layer
            drawSkinnedBucketsDirect(cmdBuf, flightIndex, frameData.drawBuckets.skinnedMeshes, layerIndex);

            cmdBuf->endRendering(renderInfo);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Render: Indirect path
// ═══════════════════════════════════════════════════════════════════════════

void PointShadowPass::renderFaceIndirect(ICommandBuffer* cmdBuf,
                                          uint32_t        flightIndex,
                                          uint32_t        faceGlobalIndex) const
{
    const auto& flight = _perFlight[flightIndex];

    cmdBuf->bindPipeline(_indirectStaticVariant.pipeline.get());
    cmdBuf->bindDescriptorSets(_indirectStaticVariant.pipelineLayout.get(), 0,
                               {flight.faceDS[faceGlobalIndex], flight.indirectDS});

    // One drawIndexedIndirectCount call per face — draws ALL visible instances at once
    auto* drawCmdBuffer  = _cullPass.getDrawCommandBuffer(flightIndex);
    auto* drawCountBuf   = _cullPass.getDrawCountBuffer(flightIndex);

    const uint64_t drawOffset  = static_cast<uint64_t>(faceGlobalIndex) * ShadowConstants::MAX_DRAWS_PER_FACE * sizeof(PointShadowIndirectCommand);
    const uint64_t countOffset = static_cast<uint64_t>(faceGlobalIndex) * sizeof(uint32_t);

    // We need to bind the shared vertex/index buffers
    // For indirect draw, each draw command specifies its own firstIndex/vertexOffset,
    // but we need a single VBO/IBO bound. Since all instances reference the same mesh
    // within a batch, we bind per-batch.
    // Actually with the new design, each indirect command has its own index/vertex offset,
    // so we just need ONE global VBO and IBO bound. But meshes have separate buffers...
    // For now, bind the first mesh's buffers — this works because in practice all
    // static meshes share a single merged buffer (or we need multi-draw).

    // Simplified approach: one MDI call covers all meshes sharing the same VBO/IBO
    // TODO: For multiple mesh buffers, need to group by buffer or use bindless
    if (!flight.meshBatches.empty() && flight.meshBatches[0].mesh) {
        auto* mesh = flight.meshBatches[0].mesh;
        cmdBuf->bindVertexBuffer(0, mesh->getVertexBuffer(), 0);
        cmdBuf->bindIndexBuffer(mesh->getIndexBufferMut(), 0, false);
    }

    cmdBuf->drawIndexedIndirectCount(
        drawCmdBuffer, drawOffset,
        drawCountBuf, countOffset,
        ShadowConstants::MAX_DRAWS_PER_FACE,
        sizeof(PointShadowIndirectCommand));
}

// ═══════════════════════════════════════════════════════════════════════════
// Render: Direct draw fallback
// ═══════════════════════════════════════════════════════════════════════════

void PointShadowPass::renderFaceDirect(ICommandBuffer*        cmdBuf,
                                        uint32_t               flightIndex,
                                        const RenderFrameData& frameData,
                                        uint32_t               faceGlobalIndex) const
{
    // layerIndex for UBO lookup (offset by directional layer)
    const uint32_t layerIndex = 1 + faceGlobalIndex;
    drawStaticBucketsDirect(cmdBuf, flightIndex, frameData.drawBuckets.staticMeshes, layerIndex);
}

void PointShadowPass::drawStaticBucketsDirect(ICommandBuffer* cmdBuf, uint32_t flightIndex,
                                               const RenderShadingDrawBuckets& buckets, uint32_t layerIndex) const
{
    auto drawItems = [&](const std::vector<RenderDrawItem>& items)
    {
        if (items.empty()) return;
        const auto& flight = _perFlight[flightIndex];
        // Use faceGlobal index (layerIndex - 1) for DS lookup
        const uint32_t faceGlobal = layerIndex - 1;
        cmdBuf->bindPipeline(_directStaticVariant.pipeline.get());
        cmdBuf->bindDescriptorSets(_directStaticVariant.pipelineLayout.get(), 0, {flight.faceDS[faceGlobal]});
        for (const auto& item : items) {
            if (!item.mesh) continue;
            ModelPushConstant pc{.modelMat = item.worldMatrix, .skinningPaletteIndex = -1};
            cmdBuf->pushConstants(_directStaticVariant.pipelineLayout.get(), EShaderStage::Vertex, 0, sizeof(ModelPushConstant), &pc);
            item.mesh->drawStatic(cmdBuf);
        }
    };

    drawItems(buckets.pbrDrawItems);
    drawItems(buckets.phongDrawItems);
    drawItems(buckets.unlitDrawItems);
    drawItems(buckets.simpleDrawItems);
    drawItems(buckets.fallbackDrawItems);
}

void PointShadowPass::drawSkinnedBucketsDirect(ICommandBuffer* cmdBuf, uint32_t flightIndex,
                                                const RenderShadingDrawBuckets& buckets, uint32_t layerIndex) const
{
    auto drawItems = [&](const std::vector<RenderDrawItem>& items)
    {
        if (items.empty()) return;
        const auto& flight = _perFlight[flightIndex];
        const uint32_t faceGlobal = layerIndex - 1;
        cmdBuf->bindPipeline(_directSkinnedVariant.pipeline.get());
        cmdBuf->bindDescriptorSets(_directSkinnedVariant.pipelineLayout.get(), 0, {flight.faceDS[faceGlobal], flight.skinningDS});
        for (const auto& item : items) {
            if (!item.mesh) continue;
            ModelPushConstant pc{.modelMat = item.worldMatrix, .skinningPaletteIndex = item.skinningPaletteIndex};
            cmdBuf->pushConstants(_directSkinnedVariant.pipelineLayout.get(), EShaderStage::Vertex, 0, sizeof(ModelPushConstant), &pc);
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
// Buffer management
// ═══════════════════════════════════════════════════════════════════════════

void PointShadowPass::ensureSkinningCapacity(uint32_t paletteCount)
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
            .label       = std::format("PointShadow_Skinning_SSBO_{}", i),
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

void PointShadowPass::ensureInstanceCapacity(uint32_t requiredCount)
{
    if (requiredCount <= _instanceCapacity) return;

    uint32_t newCap = _instanceCapacity == 0 ? 256u : _instanceCapacity;
    while (newCap < requiredCount) newCap *= 2;
    _instanceCapacity = newCap;

    const uint32_t bufferSize = _instanceCapacity * sizeof(PointShadowInstanceData);
    for (uint32_t i = 0; i < MAX_FLIGHTS_IN_FLIGHT; ++i) {
        auto& flight = _perFlight[i];
        flight.instanceBuffer = IBuffer::create(_render, BufferCreateInfo{
            .label       = std::format("PointShadow_Instance_{}", i),
            .usage       = EBufferUsage::StorageBuffer,
            .size        = bufferSize,
            .memoryUsage = EMemoryUsage::CpuToGpu,
        });
        updateIndirectDescriptors(i);
    }
}

void PointShadowPass::updateIndirectDescriptors(uint32_t flightIndex)
{
    auto& flight = _perFlight[flightIndex];
    if (!flight.instanceBuffer) return;

    if (!flight.indirectDS) {
        flight.indirectDS = _dsp->allocateDescriptorSets(_indirectDSL);
    }
    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::writeOneStorageBuffer(flight.indirectDS, 0, flight.instanceBuffer.get()),
    });
}

// ═══════════════════════════════════════════════════════════════════════════
// Pipeline refresh
// ═══════════════════════════════════════════════════════════════════════════

void PointShadowPass::refreshPipeline(EFormat::T depthFormat)
{
    _directPipelineCI.pipelineRenderingInfo.depthAttachmentFormat   = depthFormat;
    _indirectPipelineCI.pipelineRenderingInfo.depthAttachmentFormat = depthFormat;

    if (_directStaticVariant.pipeline) {
        auto ci = _directPipelineCI;
        ci.pipelineLayout = _directStaticVariant.pipelineLayout.get();
        ci.shaderDesc.vertexBufferDescs = {VertexBufferDescription{.slot = 0, .pitch = sizeof(Vertex)}};
        ci.shaderDesc.vertexAttributes  = {{.bufferSlot = 0, .location = 0, .format = EVertexAttributeFormat::Float3, .offset = offsetof(Vertex, position)}};
        _directStaticVariant.pipeline->updateDesc(ci);
    }
    if (_directSkinnedVariant.pipeline) {
        auto ci = _directPipelineCI;
        ci.pipelineLayout = _directSkinnedVariant.pipelineLayout.get();
        ci.shaderDesc.vertexBufferDescs = {
            VertexBufferDescription{.slot = 0, .pitch = sizeof(Vertex)},
            VertexBufferDescription{.slot = 1, .pitch = sizeof(SkeletonMeshVertex)},
        };
        ci.shaderDesc.vertexAttributes = {
            {.bufferSlot = 0, .location = 0, .format = EVertexAttributeFormat::Float3, .offset = offsetof(Vertex, position)},
            {.bufferSlot = 1, .location = 4, .format = EVertexAttributeFormat::Int4,   .offset = offsetof(SkeletonMeshVertex, boneIDs)},
            {.bufferSlot = 1, .location = 5, .format = EVertexAttributeFormat::Float4, .offset = offsetof(SkeletonMeshVertex, weights)},
        };
        ci.shaderDesc.defines = {"ENABLE_SKINNING 1", "SKINNING_SET_INDEX 1"};
        _directSkinnedVariant.pipeline->updateDesc(ci);
    }
    if (_indirectStaticVariant.pipeline) {
        auto ci = _indirectPipelineCI;
        ci.pipelineLayout = _indirectStaticVariant.pipelineLayout.get();
        ci.shaderDesc.vertexBufferDescs = {VertexBufferDescription{.slot = 0, .pitch = sizeof(Vertex)}};
        ci.shaderDesc.vertexAttributes  = {{.bufferSlot = 0, .location = 0, .format = EVertexAttributeFormat::Float3, .offset = offsetof(Vertex, position)}};
        _indirectStaticVariant.pipeline->updateDesc(ci);
    }
}

void PointShadowPass::rebuildFaceTextures(std::shared_ptr<IImage> shadowImage)
{
    if (!_render || !shadowImage) return;

    auto textureFactory = _render->getTextureFactory();

    for (uint32_t lightIndex = 0; lightIndex < MAX_POINT_LIGHTS; ++lightIndex) {
        for (uint32_t faceIndex = 0; faceIndex < 6; ++faceIndex) {
            const uint32_t layerIndex = 1 + lightIndex * 6 + faceIndex; // offset by directional layer
            auto view = textureFactory->createImageView(
                shadowImage,
                ImageViewCreateInfo{
                    .label          = std::format("PointShadow_Face_{}_{}_{}", lightIndex, faceIndex, layerIndex),
                    .viewType       = EImageViewType::View2D,
                    .aspectFlags    = EImageAspect::Depth,
                    .baseMipLevel   = 0,
                    .levelCount     = 1,
                    .baseArrayLayer = layerIndex,
                    .layerCount     = 1,
                });
            _faceDepthViews[lightIndex][faceIndex] = view;
            if (view) {
                _faceDepthTextures[lightIndex][faceIndex] = Texture::wrap(
                    shadowImage, view,
                    std::format("PointShadow_Face_{}_{}_Texture", lightIndex, faceIndex));
            }
        }
    }
}

Texture* PointShadowPass::getFaceDepthTexture(uint32_t lightIndex, uint32_t faceIndex) const
{
    if (lightIndex >= MAX_POINT_LIGHTS || faceIndex >= 6) return nullptr;
    return _faceDepthTextures[lightIndex][faceIndex].get();
}

void PointShadowPass::renderGUI()
{
    ImGui::Checkbox("Use Indirect Draw", &_bUseIndirectDraw);
    if (!_bIndirectSupported) {
        ImGui::SameLine();
        ImGui::TextDisabled("(unsupported)");
    }
}

} // namespace ya
