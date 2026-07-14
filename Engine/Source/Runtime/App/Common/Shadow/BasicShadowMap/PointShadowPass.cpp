#include "PointShadowPass.h"

#include "Render/Core/RenderGraphImportUtils.h"

#include "Core/Profiling/Instrumentor.h"
#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"

#include "Runtime/App/Common/Shadow/Common/ShadowDrawHelper.h"
#include "Runtime/App/Common/Shadow/Common/ShadowMapResources.h"

#include "Render/Core/CommandBuffer.h"
#include "Render/Core/RenderResourceFactory.h"
#include "Render/Core/Texture.h"
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
    _graphExecutor = std::make_unique<RenderGraphExecutor>(*_render->getResourceFactory());

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
            flight.faceUBO[face] = _render->getResourceFactory()->createBuffer(BufferCreateInfo{
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
    _shadowImage.reset();
    _graphExecutor.reset();
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
    YA_PROFILE_FUNCTION();
    if (!payload.frameData) return;

    auto& flight = _perFlight[payload.flightIndex];

    {
        YA_PROFILE_SCOPE("PointShadowPass::BeginFrame");
        if (_directStaticVariant.pipeline)  _directStaticVariant.pipeline->beginFrame();
        if (_directSkinnedVariant.pipeline) _directSkinnedVariant.pipeline->beginFrame();
        _indirectRenderer.beginFrame();
    }

    if (payload.pointLightCount == 0) return;

    {
        YA_PROFILE_SCOPE("PointShadowPass::FaceUBO");
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
    }

    {
        YA_PROFILE_SCOPE("PointShadowPass::SkinningUpload");
        const auto& palettes = payload.frameData->skinningPalettes;
        ensureSkinningCapacity(static_cast<uint32_t>(palettes.size()));
        if (!palettes.empty()) {
            flight.skinningSSBO->writeData(palettes.data(), palettes.size() * sizeof(RenderSkinningPalette), 0);
            flight.skinningSSBO->flush();
        }
    }

    {
        YA_PROFILE_SCOPE("PointShadowPass::IndirectPrepare");
        _indirectRenderer.prepare(payload);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Execute
// ═══════════════════════════════════════════════════════════════════════════

void PointShadowPass::execute(ICommandBuffer* cmdBuf, const BasicShadowFramePayload& payload)
{
    YA_PROFILE_FUNCTION();
    if (!cmdBuf || !_graphExecutor) return;

    RenderGraph graph;
    if (!appendGraphPasses(graph, payload).has_value()) return;
    YA_CORE_ASSERT(_graphExecutor->execute(graph, *cmdBuf),
                   "Failed to execute point shadow graph");
}

std::optional<RGPassHandle> PointShadowPass::appendGraphPasses(
    RenderGraph& graph,
    const BasicShadowFramePayload& payload,
    std::optional<RGPassHandle> dependency)
{
    if (!payload.frameData || payload.pointLightCount == 0 || !_shadowImage) return std::nullopt;

    auto& flight = _perFlight[payload.flightIndex];
    const bool useIndirect = payload.pointIndirectRequested() && _indirectRenderer.hasRenderableInstances(payload.flightIndex);

    std::optional<RGPassHandle> rasterDependency = dependency;

    YA_CORE_ASSERT(flight.skinningSSBO, "Point shadow graph requires a skinning buffer");

    const auto importBuffer = [&](IBuffer* buffer,
                                  std::string label,
                                  EBufferUsage usage,
                                  BufferResourceState initialState) {
        return graph.importBuffer(RGImportedBufferDesc{
            .desc = RGBufferDesc{
                .label = std::move(label),
                .usage = usage,
                .size  = buffer->getSize(),
            },
            .buffer       = buffer,
            .initialState = initialState,
        });
    };
    const BufferResourceState hostWriteState{
        .stages = EPipelineStage::Host,
        .access = EResourceAccess::HostWrite,
    };
    const auto skinningBuffer = importBuffer(
        flight.skinningSSBO.get(), "PointShadow.SkinningSSBO", EBufferUsage::StorageBuffer, hostWriteState);

    std::optional<RGBufferHandle> drawCommands;
    std::optional<RGBufferHandle> visibleInstances;
    if (useIndirect) {
        auto& cullPass = _indirectRenderer.getCullPass();
        const bool gpuCullEnabled = payload.pointIndirectCullEnabled();
        const auto cullResources = cullPass.appendGraphPass(
            graph, payload.flightIndex, gpuCullEnabled, dependency);
        YA_CORE_ASSERT(cullResources.has_value(),
                       "Point shadow graph requires cull resources");
        drawCommands     = cullResources->drawCommands;
        visibleInstances = cullResources->visibleInstances;
        if (cullResources->cullPass.has_value()) {
            rasterDependency = cullResources->cullPass;
        }
    }

    struct GraphFace
    {
        PointShadowFacePayload payload{};
        RGTextureHandle        depth{};
        RGBufferHandle         faceBuffer{};
    };
    auto graphFaces = std::make_shared<std::vector<GraphFace>>();
    graphFaces->reserve(payload.pointLightCount * 6);

    for (uint32_t lightIndex = 0; lightIndex < payload.pointLightCount; ++lightIndex) {
        for (uint32_t faceIndex = 0; faceIndex < 6; ++faceIndex) {
            PointShadowFacePayload facePayload{
                .lightIndex      = lightIndex,
                .faceIndex       = faceIndex,
                .faceGlobalIndex = lightIndex * 6 + faceIndex,
                .layerIndex      = getShadowPointLightBaseLayer(lightIndex) + faceIndex,
            };
            facePayload.faceDS = flight.faceDS[facePayload.faceGlobalIndex];
            facePayload.depthImage = _shadowImage.get();
            facePayload.depthView  = _faceDepthViews[lightIndex][faceIndex].get();
            auto faceDepthView = _faceDepthViews[lightIndex][faceIndex];
            auto* faceUBO = flight.faceUBO[facePayload.faceGlobalIndex].get();
            if (!facePayload.depthView || !faceDepthView || !faceUBO) continue;

            const auto depth = graph.importTexture(makeImportedTextureDesc(
                _shadowImage,
                faceDepthView,
                std::format("PointShadow.Depth.{}.{}", lightIndex, faceIndex),
                EImageLayout::ShaderReadOnlyOptimal,
                EImageUsage::DepthStencilAttachment,
                Extent3D{_shadowExtent.width, _shadowExtent.height, 1}));
            const auto faceBuffer = importBuffer(
                faceUBO,
                std::format("PointShadow.FaceUBO.{}.{}", lightIndex, faceIndex),
                EBufferUsage::UniformBuffer,
                hostWriteState);

            graphFaces->push_back({
                .payload    = facePayload,
                .depth      = depth,
                .faceBuffer = faceBuffer,
            });
        }
    }

    if (graphFaces->empty()) return rasterDependency;

    const auto rasterPass = graph.addPass(
        "Point Shadow Faces",
        [graphFaces, skinningBuffer, drawCommands, visibleInstances, rasterDependency](RGPassBuilder& pass) {
            if (rasterDependency.has_value()) pass.dependsOn(*rasterDependency);
            pass.read(skinningBuffer);
            if (drawCommands.has_value()) pass.indirectRead(*drawCommands);
            if (visibleInstances.has_value()) pass.read(*visibleInstances);
            for (const auto& face : *graphFaces) {
                pass.read(face.faceBuffer);
                pass.useDepthAttachment(face.depth);
            }
        },
        [this, payload, useIndirect, graphFaces](RGRenderContext& ctx) {
            YA_PERF_SCOPE(perf::sample::shadowPoint(), perf::metric::cpuTimeMs(), perf::domain::render());
            YA_PROFILE_SCOPE("PointShadowPass::RenderFaces");
            YA_PERF_SCOPE(perf::sample::shadowPointFaceLoop(), perf::metric::cpuTimeMs(), perf::domain::render());

            auto& commandBuffer = ctx.getCommandBuffer();
            for (const auto& face : *graphFaces) {
                const auto& facePayload = face.payload;
                const auto  depth       = face.depth;

                ctx.beginRasterRendering({
                    .renderArea = Rect2D{.pos = {0.0f, 0.0f}, .extent = _shadowExtent.toVec2()},
                    .layerCount = 1,
                    .depth = RGRenderContext::DepthRenderingDesc{
                        .depth       = depth,
                        .clearValue  = ClearValue(1.0f, 0),
                        .loadOp      = EAttachmentLoadOp::Clear,
                        .storeOp     = EAttachmentStoreOp::Store,
                        .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                    },
                });

                commandBuffer.setViewport(0.0f, 0.0f, static_cast<float>(_shadowExtent.width),
                                          static_cast<float>(_shadowExtent.height), 0.0f, 1.0f);
                commandBuffer.setScissor(0, 0, _shadowExtent.width, _shadowExtent.height);

                if (useIndirect) {
                    YA_PROFILE_SCOPE("PointShadowPass::DrawFaceIndirect");
                    _indirectRenderer.renderFace(&commandBuffer, payload, facePayload);
                }
                else {
                    YA_PROFILE_SCOPE("PointShadowPass::DrawFaceDirect");
                    YA_PERF_SCOPE(perf::sample::shadowPointFaceDirect(), perf::metric::cpuTimeMs(), perf::domain::render());
                    renderFaceDirect(&commandBuffer, payload, facePayload);
                }

                {
                    YA_PROFILE_SCOPE("PointShadowPass::DrawFaceSkinned");
                    YA_PERF_SCOPE(perf::sample::shadowPointFaceSkinned(), perf::metric::cpuTimeMs(), perf::domain::render());
                    const auto& passFlight = _perFlight[payload.flightIndex];
                    ShadowDrawHelper::PassResources skinnedRes{
                        .pipeline       = _directSkinnedVariant.pipeline.get(),
                        .pipelineLayout = _directSkinnedVariant.pipelineLayout.get(),
                        .frameDS        = passFlight.faceDS[facePayload.faceGlobalIndex],
                        .skinningDS     = passFlight.skinningDS,
                    };
                    ShadowDrawHelper::drawSkinnedBuckets(
                        &commandBuffer, skinnedRes, payload.frameData->drawBuckets.skinnedMeshes);
                }
                ctx.endRendering();
            }
        });

    return rasterPass;
}

// ═══════════════════════════════════════════════════════════════════════
// Render: Direct draw fallback
// ═══════════════════════════════════════════════════════════════════════

void PointShadowPass::renderFaceDirect(ICommandBuffer*                 cmdBuf,
                                        const BasicShadowFramePayload& payload,
                                        const PointShadowFacePayload&  facePayload) const
{
    YA_PROFILE_FUNCTION();
    const auto& flight = _perFlight[payload.flightIndex];
    ShadowDrawHelper::PassResources staticRes{
        .pipeline       = _directStaticVariant.pipeline.get(),
        .pipelineLayout = _directStaticVariant.pipelineLayout.get(),
        .frameDS        = flight.faceDS[facePayload.faceGlobalIndex],
    };
    ShadowDrawHelper::drawStaticBuckets(cmdBuf, staticRes, payload.frameData->drawBuckets.staticMeshes);
}

// ═══════════════════════════════════════════════════════════════════════
// Buffer management
// ═══════════════════════════════════════════════════════════════════════
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
        flight.skinningSSBO = _render->getResourceFactory()->createBuffer(BufferCreateInfo{
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

    _shadowImage = shadowImage;
    auto* resourceFactory = _render->getResourceFactory();

    for (uint32_t lightIndex = 0; lightIndex < MAX_POINT_LIGHTS; ++lightIndex) {
        for (uint32_t faceIndex = 0; faceIndex < 6; ++faceIndex) {
            const uint32_t layerIndex = getShadowPointLightBaseLayer(lightIndex) + faceIndex;
            auto view = resourceFactory->createImageView(
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
        }
    }
}

void PointShadowPass::renderGUI()
{
    ImGui::Text("Indirect Path: %s", _indirectRenderer.isSupported() ? "supported" : "unsupported");
}

} // namespace ya
