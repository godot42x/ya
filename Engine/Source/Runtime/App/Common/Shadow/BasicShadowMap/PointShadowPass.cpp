#include "PointShadowPass.h"

#include "Render/Core/CommandBuffer.h"
#include "Render/Core/Texture.h"
#include "Render/Core/TextureFactory.h"
#include "Render/Mesh.h"
#include "Render/Render.h"
#include "Render/RenderFrameData.h"

#include "imgui.h"

#include <format>
#include <vector>

namespace ya
{

// ═══════════════════════════════════════════════════════════════════════════
// Init / Destroy
// ═══════════════════════════════════════════════════════════════════════════

void PointShadowPass::init(IRender* render, Extent2D shadowExtent)
{
    _render       = render;
    _shadowExtent = shadowExtent;

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

    // ─── Pipeline: direct draw (CombineShadowMappingGenerate.slang) ──
    _directPipelineCI = GraphicsPipelineCreateInfo{
        .pipelineRenderingInfo = {.label = "Point Shadow Direct", .depthAttachmentFormat = EFormat::D32_SFLOAT},
        .shaderDesc            = ShaderDesc{.shaderName = "CombineShadowMappingGenerate.slang"},
        .dynamicFeatures       = {EPipelineDynamicFeature::Viewport, EPipelineDynamicFeature::Scissor},
        .primitiveType         = EPrimitiveType::TriangleList,
        .rasterizationState    = {.polygonMode = EPolygonMode::Fill, .cullMode = ECullMode::Front, .frontFace = EFrontFaceType::CounterClockWise},
        .depthStencilState     = {
            .bDepthTestEnable  = true,
            .bDepthWriteEnable = true,
            .depthCompareOp    = ECompareOp::LessOrEqual,
        },
        .colorBlendState = {.attachments = {}},
        .viewportState   = {
            .viewports = {Viewport::defaults()},
            .scissors  = {Scissor::defaults()},
        },
    };

    // Static Mesh
    _directStaticVariant.pipelineLayout = IPipelineLayout::create(
        _render, "PointShadow_Direct_Static_PPL",
        {PushConstantRange{.offset = 0, .size = sizeof(ModelPushConstant), .stageFlags = EShaderStage::Vertex}},
        {_frameDSL});

    auto directStaticCI = _directPipelineCI;
    directStaticCI.pipelineLayout = _directStaticVariant.pipelineLayout.get();
    directStaticCI.shaderDesc.vertexBufferDescs = {VertexBufferDescription{.slot = 0, .pitch = sizeof(Vertex)}};
    directStaticCI.shaderDesc.vertexAttributes  = {{.bufferSlot = 0, .location = 0, .format = EVertexAttributeFormat::Float3, .offset = offsetof(Vertex, position)}};
    directStaticCI.shaderDesc.defines = {"POINT_SHADOW_FACE_DATA 1"};
    _directStaticVariant.pipeline = IGraphicsPipeline::create(_render);
    YA_CORE_ASSERT(_directStaticVariant.pipeline && _directStaticVariant.pipeline->recreate(directStaticCI),
                   "Failed to create point shadow direct static pipeline");

    // Skinning
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
    directSkinnedCI.shaderDesc.defines = {"POINT_SHADOW_FACE_DATA 1", "ENABLE_SKINNING 1", "SKINNING_SET_INDEX 1"};
    _directSkinnedVariant.pipeline = IGraphicsPipeline::create(_render);
    YA_CORE_ASSERT(_directSkinnedVariant.pipeline && _directSkinnedVariant.pipeline->recreate(directSkinnedCI),
                   "Failed to create point shadow direct skinned pipeline");

    _indirectRenderer.init(_render, _frameDSL);

    // ─── Descriptor pool ─────────────────────────────────────────────
    const uint32_t faceCount   = ShadowConstants::POINT_SHADOW_FACE_COUNT;
    const uint32_t maxSets     = MAX_FLIGHTS_IN_FLIGHT * (faceCount + 1); // face UBOs + skinning
    const uint32_t uboCount    = MAX_FLIGHTS_IN_FLIGHT * faceCount;
    const uint32_t ssboCount   = MAX_FLIGHTS_IN_FLIGHT;
    _dsp = IDescriptorPool::create(_render, DescriptorPoolCreateInfo{
        .label     = "PointShadow_DSP",
        .maxSets   = maxSets,
        .poolSizes = {
            {.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = uboCount},
            {.type = EPipelineDescriptorType::StorageBuffer, .descriptorCount = ssboCount},
        },
    });

    // ─── Per-flight resources ────────────────────────────────────────
    PointFaceUBO initialData{};
    for (uint32_t fi = 0; fi < MAX_FLIGHTS_IN_FLIGHT; ++fi) {
        auto& flight = _perFlight[fi];
        for (uint32_t face = 0; face < faceCount; ++face) {
            flight.faceUBO[face] = IBuffer::create(_render, BufferCreateInfo{
                .label       = std::format("PointShadow_FaceUBO_{}_{}", fi, face),
                .usage       = EBufferUsage::UniformBuffer,
                .size        = sizeof(PointFaceUBO),
                .memoryUsage = EMemoryUsage::CpuToGpu,
            });
            flight.faceUBO[face]->writeData(&initialData, sizeof(PointFaceUBO), 0);

            flight.faceDS[face] = _dsp->allocateDescriptorSets(_frameDSL);
            _render->getDescriptorHelper()->updateDescriptorSets({
                IDescriptorSetHelper::writeOneUniformBuffer(flight.faceDS[face], 0, flight.faceUBO[face].get()),
            });
        }
    }
}

void PointShadowPass::destroy()
{
    _indirectRenderer.destroy();

    for (auto& flight : _perFlight) {
        for (auto& ubo : flight.faceUBO) ubo.reset();
        flight.skinningSSBO.reset();
        flight.skinningDS  = nullptr;
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
    _skinningDSL.reset();
    _frameDSL.reset();
    _skinningCapacity = 0;
    _render = nullptr;
}

// ═══════════════════════════════════════════════════════════════════════════
// Prepare
// ═══════════════════════════════════════════════════════════════════════════

void PointShadowPass::prepare(const BasicShadowFramePayload& payload)
{
    if (!payload.frameData) return;

    auto& flight = _perFlight[payload.flightIndex];

    // Begin frame for pipelines
    if (_directStaticVariant.pipeline)  _directStaticVariant.pipeline->beginFrame();
    if (_directSkinnedVariant.pipeline) _directSkinnedVariant.pipeline->beginFrame();
    _indirectRenderer.beginFrame();

    if (payload.pointLightCount == 0) return;

    // ─── Write per-face UBOs ─────────────────────────────────────────
    for (uint32_t lightIndex = 0; lightIndex < payload.pointLightCount; ++lightIndex) {
        for (uint32_t faceIndex = 0; faceIndex < 6; ++faceIndex) {
            PointFaceUBO faceData{
                .viewProj  = payload.frameUBO.pointLights[lightIndex].matrix[faceIndex],
                .lightPos  = payload.frameUBO.pointLights[lightIndex].pos,
                .farPlane  = payload.frameUBO.pointLights[lightIndex].farPlane,
            };

            const uint32_t faceGlobal = lightIndex * 6 + faceIndex;
            flight.faceUBO[faceGlobal]->writeData(&faceData, sizeof(PointFaceUBO), 0);
        }
    }

    // ─── Skinning ────────────────────────────────────────────────────
    const auto& palettes = payload.frameData->skinningPalettes;
    ensureSkinningCapacity(static_cast<uint32_t>(palettes.size()));
    if (!palettes.empty()) {
        flight.skinningSSBO->writeData(palettes.data(), palettes.size() * sizeof(RenderSkinningPalette), 0);
        flight.skinningSSBO->flush();
    }

    _indirectRenderer.prepare(payload);
}

// ═══════════════════════════════════════════════════════════════════════════
// Execute
// ═══════════════════════════════════════════════════════════════════════════

void PointShadowPass::execute(ICommandBuffer* cmdBuf, const BasicShadowFramePayload& payload)
{
    if (!payload.frameData || payload.pointLightCount == 0) return;

    auto& flight = _perFlight[payload.flightIndex];
    const bool useIndirect = payload.pointIndirectRequested && _indirectRenderer.hasRenderableInstances(payload.flightIndex);

    // Dispatch compute cull if using indirect
    if (useIndirect) {
        _indirectRenderer.dispatchCull(cmdBuf, payload.flightIndex);
    }

    // Render each face
    for (uint32_t lightIndex = 0; lightIndex < payload.pointLightCount; ++lightIndex) {
        for (uint32_t faceIndex = 0; faceIndex < 6; ++faceIndex) {
            PointShadowFacePayload facePayload{
                .lightIndex      = lightIndex,
                .faceIndex       = faceIndex,
                .faceGlobalIndex = lightIndex * 6 + faceIndex,
                .layerIndex      = 1 + lightIndex * 6 + faceIndex,
            };
            facePayload.faceDS = flight.faceDS[facePayload.faceGlobalIndex];
            facePayload.depthTexture = _faceDepthTextures[lightIndex][faceIndex].get();
            if (!facePayload.depthTexture) continue;

            RenderingInfo::ImageSpec depthSpec{
                .texture       = facePayload.depthTexture,
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
                _indirectRenderer.renderFace(cmdBuf, payload, facePayload);
            } else {
                renderFaceDirect(cmdBuf, payload, facePayload);
            }

            // Always draw skinned meshes with direct draw (no indirect path for skinned yet)
            drawSkinnedBucketsDirect(cmdBuf, payload.flightIndex, payload.frameData->drawBuckets.skinnedMeshes, facePayload.layerIndex);

            cmdBuf->endRendering(renderInfo);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Render: Direct draw fallback
// ═══════════════════════════════════════════════════════════════════════════

void PointShadowPass::renderFaceDirect(ICommandBuffer*                 cmdBuf,
                                        const BasicShadowFramePayload& payload,
                                        const PointShadowFacePayload&  facePayload) const
{
    drawStaticBucketsDirect(cmdBuf, payload.flightIndex, payload.frameData->drawBuckets.staticMeshes, facePayload.layerIndex);
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

// ═══════════════════════════════════════════════════════════════════════════
// Pipeline refresh
// ═══════════════════════════════════════════════════════════════════════════

void PointShadowPass::refreshPipeline(EFormat::T depthFormat)
{
    _directPipelineCI.pipelineRenderingInfo.depthAttachmentFormat = depthFormat;

    if (_directStaticVariant.pipeline) {
        auto ci = _directPipelineCI;
        ci.pipelineLayout = _directStaticVariant.pipelineLayout.get();
        ci.shaderDesc.vertexBufferDescs = {VertexBufferDescription{.slot = 0, .pitch = sizeof(Vertex)}};
        ci.shaderDesc.vertexAttributes  = {{.bufferSlot = 0, .location = 0, .format = EVertexAttributeFormat::Float3, .offset = offsetof(Vertex, position)}};
        ci.shaderDesc.defines = {"POINT_SHADOW_FACE_DATA 1"};
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
        ci.shaderDesc.defines = {"POINT_SHADOW_FACE_DATA 1", "ENABLE_SKINNING 1", "SKINNING_SET_INDEX 1"};
        _directSkinnedVariant.pipeline->updateDesc(ci);
    }
    _indirectRenderer.refreshPipeline(depthFormat);
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
    ImGui::Text("Indirect Draw: %s", _bUseIndirectDraw ? "On" : "Off");
    if (!_indirectRenderer.isSupported()) {
        ImGui::SameLine();
        ImGui::TextDisabled("(unsupported)");
    }
}

} // namespace ya
