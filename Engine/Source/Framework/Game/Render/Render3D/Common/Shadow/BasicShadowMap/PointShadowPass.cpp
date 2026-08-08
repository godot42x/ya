#include "PointShadowPass.h"

#include "Graph/RenderGraphImportUtils.h"

#include "Core/Profiling/Instrumentor.h"
#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"

#include "Render3D/Common/Shadow/Common/ShadowDrawHelper.h"
#include "Render3D/Common/Shadow/Common/ShadowMapResources.h"

#include "RHI/Core/RenderResourceFactory.h"
#include "RHI/Core/Texture.h"
#include "Resource/Mesh.h"
#include "RHI/Render.h"
#include "Render3D/RenderFrameData.h"

#include <format>
#include <vector>

namespace ya
{

// ═══════════════════════════════════════════════════════════════════════════
// Init / Destroy
// ═══════════════════════════════════════════════════════════════════════════

void PointShadowPass::init(IRender* render,
                           Extent2D shadowExtent,
                           ShadowFrameResources& frameResources)
{
    _render         = render;
    _frameResources = &frameResources;
    _shadowExtent   = shadowExtent;

    _frameDSL    = _frameResources->getFrameDSL();
    _skinningDSL = _frameResources->getSkinningDSL();

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

}

void PointShadowPass::destroy()
{
    _indirectRenderer.destroy();

    _shadowImage.reset();
    for (auto& faceViewArr : _faceDepthViews) {
        for (auto& view : faceViewArr) view.reset();
    }
    _directStaticVariant  = {};
    _directSkinnedVariant = {};
    _skinningDSL.reset();
    _frameDSL.reset();
    _frameResources = nullptr;
    _render = nullptr;
}

// ═══════════════════════════════════════════════════════════════════════════
// Prepare
// ═══════════════════════════════════════════════════════════════════════════

void PointShadowPass::prepare(const BasicShadowFramePayload& payload)
{
    YA_PROFILE_FUNCTION();
    if (!payload.frameData) return;

    {
        YA_PROFILE_SCOPE("PointShadowPass::BeginFrame");
        if (_directStaticVariant.pipeline)  _directStaticVariant.pipeline->beginFrame();
        if (_directSkinnedVariant.pipeline) _directSkinnedVariant.pipeline->beginFrame();
        _indirectRenderer.beginFrame();
    }

    if (payload.pointLightCount == 0) return;

    {
        YA_PROFILE_SCOPE("PointShadowPass::IndirectPrepare");
        _indirectRenderer.prepare(payload);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Execute
// ═══════════════════════════════════════════════════════════════════════════

std::optional<RGPassHandle> PointShadowPass::appendGraphPasses(
    RenderGraph& graph,
    const BasicShadowFramePayload& payload,
    std::optional<RGPassHandle> dependency)
{
    if (!payload.frameData || payload.pointLightCount == 0 || !_shadowImage) return std::nullopt;

    const bool useIndirect = payload.pointIndirectRequested() && _indirectRenderer.hasRenderableInstances(payload.flightIndex);

    std::optional<RGPassHandle> rasterDependency = dependency;

    YA_CORE_ASSERT(_frameResources != nullptr, "Point shadow graph requires frame resources");
    const auto& binding = _frameResources->getBinding(payload.flightIndex);
    YA_CORE_ASSERT(binding.skinningBuffer, "Point shadow graph requires a skinning buffer");

    const auto importBuffer = [&](const std::shared_ptr<IBuffer>& buffer,
                                  std::string label,
                                  EBufferUsage usage,
                                  BufferResourceState initialState) {
        YA_CORE_ASSERT(buffer != nullptr, "Point shadow graph requires imported buffer '{}'", label);
        return graph.importBuffer(makeImportedBufferDesc(buffer, label, initialState, usage));
    };
    const BufferResourceState hostWriteState{
        .stages = EPipelineStage::Host,
        .access = EResourceAccess::HostWrite,
    };
    const auto skinningBuffer = importBuffer(
        binding.skinningBuffer, "PointShadow.SkinningSSBO", EBufferUsage::StorageBuffer, hostWriteState);

    std::optional<RGBufferHandle> drawCommands;
    std::optional<RGBufferHandle> visibleInstances;
    std::optional<RGBufferHandle> instanceData;
    if (useIndirect) {
        auto& cullPass = _indirectRenderer.getCullPass();
        const bool gpuCullEnabled = payload.pointIndirectCullEnabled();
        const auto cullResources = cullPass.appendGraphPass(
            graph, payload.flightIndex, gpuCullEnabled, dependency);
        YA_CORE_ASSERT(cullResources.has_value(),
                       "Point shadow graph requires cull resources");
        drawCommands     = cullResources->drawCommands;
        visibleInstances = cullResources->visibleInstances;
        instanceData     = cullResources->instanceData;
        if (cullResources->cullPass.has_value()) {
            rasterDependency = cullResources->cullPass;
        }
    }

    struct GraphFace
    {
        PointShadowFacePayload payload{};
        RGTextureHandle        depth{};
        RGBufferHandle         faceBuffer{};
        RGBufferRange          faceRange{};
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
            facePayload.faceDS = binding.pointFaceDS[facePayload.faceGlobalIndex];
            facePayload.depthImage = _shadowImage.get();
            facePayload.depthView  = _faceDepthViews[lightIndex][faceIndex].get();
            auto faceDepthView = _faceDepthViews[lightIndex][faceIndex];
            const auto& faceAllocation = binding.pointFaces[facePayload.faceGlobalIndex];
            if (!facePayload.depthView || !faceDepthView || !faceAllocation) continue;

            const auto depth = graph.importTexture(makeImportedTextureDesc(
                _shadowImage,
                faceDepthView,
                std::format("PointShadow.Depth.{}.{}", lightIndex, faceIndex),
                EImageLayout::ShaderReadOnlyOptimal,
                EImageUsage::DepthStencilAttachment,
                Extent3D{_shadowExtent.width, _shadowExtent.height, 1}));
            const auto faceBuffer = importBuffer(
                faceAllocation.buffer,
                std::format("PointShadow.FaceUBO.{}.{}", lightIndex, faceIndex),
                EBufferUsage::UniformBuffer,
                BufferResourceState{
                    .stages = EPipelineStage::Host,
                    .access = EResourceAccess::HostWrite,
                    .offset = faceAllocation.offset,
                    .size   = faceAllocation.size,
                });

            graphFaces->push_back({
                .payload    = facePayload,
                .depth      = depth,
                .faceBuffer = faceBuffer,
                .faceRange  = {.offset = faceAllocation.offset, .size = faceAllocation.size},
            });
        }
    }

    if (graphFaces->empty()) return rasterDependency;

    const auto rasterPass = graph.addPass(
        "Point Shadow Faces",
        [graphFaces, skinningBuffer, instanceData, drawCommands, visibleInstances, rasterDependency](RGPassBuilder& pass) {
            if (rasterDependency.has_value()) pass.dependsOn(*rasterDependency);
            pass.storageRead(skinningBuffer);
            if (instanceData.has_value()) pass.storageRead(*instanceData);
            if (drawCommands.has_value()) pass.indirectRead(*drawCommands);
            if (visibleInstances.has_value()) pass.storageRead(*visibleInstances);
            for (const auto& face : *graphFaces) {
                pass.uniformRead(face.faceBuffer, face.faceRange);
                pass.useDepthAttachment(face.depth);
            }
        },
        [this, payload, useIndirect, graphFaces, drawCommands, visibleInstances,
         skinningDS = binding.skinningDS](RGRenderContext& ctx) {
            YA_PERF_SCOPE(perf::sample::shadowPoint(), perf::metric::cpuTimeMs(), perf::domain::render());
            YA_PROFILE_SCOPE("PointShadowPass::RenderFaces");
            YA_PERF_SCOPE(perf::sample::shadowPointFaceLoop(), perf::metric::cpuTimeMs(), perf::domain::render());

            auto& commandBuffer = ctx.getCommandBuffer();
            IBuffer* indirectCommandBuffer = nullptr;
            if (useIndirect && drawCommands.has_value()) {
                indirectCommandBuffer = ctx.resolveBuffer(*drawCommands);
                if (visibleInstances.has_value()) {
                    _indirectRenderer.bindGraphVisibleInstances(
                        payload.flightIndex,
                        ctx.resolveBuffer(*visibleInstances));
                }
            }
            if (useIndirect && !indirectCommandBuffer) return;
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
                    _indirectRenderer.renderFace(&commandBuffer, payload, facePayload, indirectCommandBuffer);
                }
                else {
                    YA_PROFILE_SCOPE("PointShadowPass::DrawFaceDirect");
                    YA_PERF_SCOPE(perf::sample::shadowPointFaceDirect(), perf::metric::cpuTimeMs(), perf::domain::render());
                    renderFaceDirect(&commandBuffer, payload, facePayload);
                }

                {
                    YA_PROFILE_SCOPE("PointShadowPass::DrawFaceSkinned");
                    YA_PERF_SCOPE(perf::sample::shadowPointFaceSkinned(), perf::metric::cpuTimeMs(), perf::domain::render());
                    ShadowDrawHelper::PassResources skinnedRes{
                        .pipeline       = _directSkinnedVariant.pipeline.get(),
                        .pipelineLayout = _directSkinnedVariant.pipelineLayout.get(),
                        .frameDS        = facePayload.faceDS,
                        .skinningDS     = skinningDS,
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
    ShadowDrawHelper::PassResources staticRes{
        .pipeline       = _directStaticVariant.pipeline.get(),
        .pipelineLayout = _directStaticVariant.pipelineLayout.get(),
        .frameDS        = facePayload.faceDS,
    };
    ShadowDrawHelper::drawStaticBuckets(cmdBuf, staticRes, payload.frameData->drawBuckets.staticMeshes);
}

// ═══════════════════════════════════════════════════════════════════════
// Buffer management
// ═══════════════════════════════════════════════════════════════════════

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

} // namespace ya
