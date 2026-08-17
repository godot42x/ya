#include "DirectionalShadowPass.h"

#include "Graph/RenderGraphImportUtils.h"

#include "Core/Profiling/Instrumentor.h"
#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"

#include "RHI/RenderDefines.h"
#include "Render3D/Common/Shadow/Common/ShadowDrawHelper.h"

#include "RHI/Core/RenderResourceFactory.h"
#include "Resource/Mesh.h"
#include "Render3D/RenderFrameData.h"
#include "RHI/Render.h"

#include <format>

namespace ya
{

// ═══════════════════════════════════════════════════════════════════════════
// Init / Destroy
// ═══════════════════════════════════════════════════════════════════════════

void DirectionalShadowPass::init(IRender* render,
                                 Extent2D shadowExtent,
                                 ShadowFrameResources& frameResources)
{
    _render         = render;
    _frameResources = &frameResources;
    _shadowExtent   = shadowExtent;

    _frameDSL    = _frameResources->getFrameDSL();
    _skinningDSL = _frameResources->getSkinningDSL();

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

}

void DirectionalShadowPass::destroy()
{
    _depthResource.reset();
    for (auto& depthView : _depthViews) depthView.reset();
    _staticVariant  = {};
    _skinnedVariant = {};
    _skinningDSL.reset();
    _frameDSL.reset();
    _frameResources = nullptr;
    _render = nullptr;
}

// ═══════════════════════════════════════════════════════════════════════════
// Prepare
// ═══════════════════════════════════════════════════════════════════════════

void DirectionalShadowPass::prepare(const BasicShadowFramePayload& payload)
{
    YA_PROFILE_FUNCTION();
    if (!payload.frameData) return;

    {
        YA_PROFILE_SCOPE("DirectionalShadowPass::BeginFrame");
        if (_staticVariant.pipeline)  _staticVariant.pipeline->beginFrame();
        if (_skinnedVariant.pipeline) _skinnedVariant.pipeline->beginFrame();
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Execute
// ═══════════════════════════════════════════════════════════════════════════

std::optional<RGPassHandle> DirectionalShadowPass::appendGraphPasses(
    RenderGraph& graph,
    const BasicShadowFramePayload& payload,
    std::optional<RGPassHandle> dependency)
{
    if (!_depthResource || !_depthResource->getImage() || !payload.frameData) return std::nullopt;

    std::optional<RGPassHandle> lastPass = dependency;
    for (uint32_t cascadeIndex = 0;
         cascadeIndex < payload.directionalCascadeCount();
         ++cascadeIndex) {
        lastPass = appendCascadePass(graph, payload, cascadeIndex, lastPass);
        if (!lastPass.has_value()) return std::nullopt;
    }
    return lastPass;
}

std::optional<RGPassHandle> DirectionalShadowPass::appendCascadePass(
    RenderGraph& graph,
    const BasicShadowFramePayload& payload,
    uint32_t cascadeIndex,
    std::optional<RGPassHandle> dependency)
{
    if (cascadeIndex >= _depthViews.size() || !_depthViews[cascadeIndex]) return std::nullopt;

    YA_CORE_ASSERT(_frameResources != nullptr, "Directional shadow graph requires frame resources");
    const auto& binding = _frameResources->getBinding(payload.flightIndex);
    YA_CORE_ASSERT(binding.directionalFrames[cascadeIndex] && binding.skinningBuffer,
                   "Directional shadow graph requires frame and skinning buffers");

    const auto depth = graph.importTexture(makeImportedTextureDesc(
        _depthResource,
        _depthViews[cascadeIndex],
        std::format("DirectionalShadow.Depth.{}", cascadeIndex),
        EImageLayout::ShaderReadOnlyOptimal,
        EImageUsage::DepthStencilAttachment,
        Extent3D{_shadowExtent.width, _shadowExtent.height, 1}));
    const auto importHostWrittenBuffer = [&](const FrameUploadArena::Allocation& allocation, std::string label, EBufferUsage usage) {
        YA_CORE_ASSERT(allocation && allocation.buffer, "Directional shadow graph requires imported buffer '{}'", label);
        return graph.importBuffer(makeHostWrittenImportedBufferDesc(
            allocation.buffer,
            label,
            usage,
            allocation.offset,
            allocation.size));
    };
    const auto frameBuffer = importHostWrittenBuffer(
        binding.directionalFrames[cascadeIndex],
        std::format("DirectionalShadow.FrameUBO.{}", cascadeIndex),
        EBufferUsage::UniformBuffer);
    const auto skinningBuffer = importHostWrittenBuffer(
        FrameUploadArena::Allocation{
            .buffer = binding.skinningBuffer,
            .offset = 0,
            .size   = binding.skinningBuffer->getSize(),
        },
        "DirectionalShadow.SkinningSSBO", EBufferUsage::StorageBuffer);
    const auto frameDS = binding.directionalFrameDS[cascadeIndex];
    const auto skinningDS = binding.skinningDS;

    const RGBufferRange frameRange{
        .offset = binding.directionalFrames[cascadeIndex].offset,
        .size   = binding.directionalFrames[cascadeIndex].size,
    };
    const auto shadowPass = graph.addPass(
        std::format("Directional Shadow Cascade {}", cascadeIndex),
        [frameBuffer, frameRange, skinningBuffer, depth, dependency](RGPassBuilder& pass) {
            if (dependency.has_value()) pass.dependsOn(*dependency);
            pass.uniformRead(frameBuffer, frameRange);
            pass.storageRead(skinningBuffer);
            pass.useDepthAttachment(depth);
        },
        [this, payload, depth, frameDS, skinningDS](RGRenderContext& ctx) {
            YA_PERF_SCOPE(perf::sample::shadowDirectional(), perf::metric::cpuTimeMs(), perf::domain::render());
            YA_PROFILE_SCOPE("DirectionalShadowPass::RenderCascade");
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

            auto& commandBuffer = ctx.getCommandBuffer();
            commandBuffer.setViewport(0.0f, 0.0f, static_cast<float>(_shadowExtent.width),
                                      static_cast<float>(_shadowExtent.height), 0.0f, 1.0f);
            commandBuffer.setScissor(0, 0, _shadowExtent.width, _shadowExtent.height);

            ShadowDrawHelper::PassResources staticRes{
                .pipeline       = _staticVariant.pipeline.get(),
                .pipelineLayout = _staticVariant.pipelineLayout.get(),
                .frameDS        = frameDS,
            };
            ShadowDrawHelper::PassResources skinnedRes{
                .pipeline       = _skinnedVariant.pipeline.get(),
                .pipelineLayout = _skinnedVariant.pipelineLayout.get(),
                .frameDS        = frameDS,
                .skinningDS     = skinningDS,
            };
            {
                YA_PROFILE_SCOPE("DirectionalShadowPass::DrawStatic");
                ShadowDrawHelper::drawStaticBuckets(&commandBuffer, staticRes, payload.frameData->drawBuckets.staticMeshes);
            }
            {
                YA_PROFILE_SCOPE("DirectionalShadowPass::DrawSkinned");
                ShadowDrawHelper::drawSkinnedBuckets(&commandBuffer, skinnedRes, payload.frameData->drawBuckets.skinnedMeshes);
            }
            ctx.endRendering();
        });
    return shadowPass;
}

// ═══════════════════════════════════════════════════════════════════════
// Skinning capacity
// ═══════════════════════════════════════════════════════════════════════

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

void DirectionalShadowPass::setDepthAttachments(
    stdptr<ImageResource> resource,
    std::array<stdptr<IImageView>, MAX_DIRECTIONAL_CASCADES> views)
{
    _depthResource = std::move(resource);
    _depthViews = std::move(views);
}

} // namespace ya
