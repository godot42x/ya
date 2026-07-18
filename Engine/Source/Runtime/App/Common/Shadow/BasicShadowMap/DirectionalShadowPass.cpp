#include "DirectionalShadowPass.h"

#include "Render/Core/RenderGraphImportUtils.h"

#include "Core/Profiling/Instrumentor.h"
#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"

#include "Runtime/App/Common/Shadow/Common/ShadowDrawHelper.h"

#include "Render/Core/CommandBuffer.h"
#include "Render/Core/RenderResourceFactory.h"
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
    _graphExecutor = std::make_unique<RenderGraphExecutor>(*_render->getResourceFactory());

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
        flight.frameUBO = _render->getResourceFactory()->createBuffer(BufferCreateInfo{
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
    _depthImage.reset();
    _depthView.reset();
    _graphExecutor.reset();
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

void DirectionalShadowPass::prepare(const BasicShadowFramePayload& payload)
{
    YA_PROFILE_FUNCTION();
    if (!payload.frameData) return;

    auto& flight = _perFlight[payload.flightIndex];

    {
        YA_PROFILE_SCOPE("DirectionalShadowPass::FrameUBO");
        FrameUBO uboData{
            .directionalLightMatrix = payload.frameUBO.directionalLightMatrix,
            .numPointLights         = 0,
            .hasDirectionalLight    = 1u,
        };
        flight.frameUBO->writeData(&uboData, sizeof(FrameUBO), 0);
    }

    {
        YA_PROFILE_SCOPE("DirectionalShadowPass::SkinningUpload");
        const auto& palettes = payload.frameData->skinningPalettes;
        ensureSkinningCapacity(static_cast<uint32_t>(palettes.size()));
        if (!palettes.empty()) {
            flight.skinningSSBO->writeData(palettes.data(), palettes.size() * sizeof(RenderSkinningPalette), 0);
            flight.skinningSSBO->flush();
        }
    }

    {
        YA_PROFILE_SCOPE("DirectionalShadowPass::BeginFrame");
        if (_staticVariant.pipeline)  _staticVariant.pipeline->beginFrame();
        if (_skinnedVariant.pipeline) _skinnedVariant.pipeline->beginFrame();
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Execute
// ═══════════════════════════════════════════════════════════════════════════

void DirectionalShadowPass::execute(ICommandBuffer* cmdBuf, const BasicShadowFramePayload& payload)
{
    YA_PROFILE_FUNCTION();
    if (!cmdBuf || !_graphExecutor) return;

    RenderGraph graph;
    if (!appendGraphPass(graph, payload).has_value()) return;
    YA_CORE_ASSERT(_graphExecutor->execute(graph, *cmdBuf),
                   "Failed to execute directional shadow graph");
}

std::optional<RGPassHandle> DirectionalShadowPass::appendGraphPass(
    RenderGraph& graph,
    const BasicShadowFramePayload& payload,
    std::optional<RGPassHandle> dependency)
{
    if (!_depthImage || !_depthView || !payload.frameData) return std::nullopt;

    const auto& flight = _perFlight[payload.flightIndex];
    YA_CORE_ASSERT(flight.frameUBO && flight.skinningSSBO,
                   "Directional shadow graph requires frame and skinning buffers");

    const auto depth = graph.importTexture(makeImportedTextureDesc(
        _depthImage,
        _depthView,
        "DirectionalShadow.Depth",
        EImageLayout::ShaderReadOnlyOptimal,
        EImageUsage::DepthStencilAttachment,
        Extent3D{_shadowExtent.width, _shadowExtent.height, 1}));
    const auto importHostWrittenBuffer = [&](const std::shared_ptr<IBuffer>& buffer, std::string label, EBufferUsage usage) {
        YA_CORE_ASSERT(buffer != nullptr, "Directional shadow graph requires imported buffer '{}'", label);
        return graph.importBuffer(RGImportedBufferDesc{
            .desc = RGBufferDesc{
                .label = std::move(label),
                .usage = usage,
                .size  = buffer->getSize(),
            },
            .buffer = buffer.get(),
            .initialState = BufferResourceState{
                .stages = EPipelineStage::Host,
                .access = EResourceAccess::HostWrite,
                .size   = buffer->getSize(),
            },
            .retainedResources = {buffer},
        });
    };
    const auto frameBuffer = importHostWrittenBuffer(
        flight.frameUBO, "DirectionalShadow.FrameUBO", EBufferUsage::UniformBuffer);
    const auto skinningBuffer = importHostWrittenBuffer(
        flight.skinningSSBO, "DirectionalShadow.SkinningSSBO", EBufferUsage::StorageBuffer);

    const auto shadowPass = graph.addPass(
        "Directional Shadow",
        [frameBuffer, skinningBuffer, depth, dependency](RGPassBuilder& pass) {
            if (dependency.has_value()) pass.dependsOn(*dependency);
            pass.read(frameBuffer);
            pass.read(skinningBuffer);
            pass.useDepthAttachment(depth);
        },
        [this, payload, depth](RGRenderContext& ctx) {
            YA_PERF_SCOPE(perf::sample::shadowDirectional(), perf::metric::cpuTimeMs(), perf::domain::render());
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

            const auto& passFlight = _perFlight[payload.flightIndex];
            ShadowDrawHelper::PassResources staticRes{
                .pipeline       = _staticVariant.pipeline.get(),
                .pipelineLayout = _staticVariant.pipelineLayout.get(),
                .frameDS        = passFlight.frameDS,
            };
            ShadowDrawHelper::PassResources skinnedRes{
                .pipeline       = _skinnedVariant.pipeline.get(),
                .pipelineLayout = _skinnedVariant.pipelineLayout.get(),
                .frameDS        = passFlight.frameDS,
                .skinningDS     = passFlight.skinningDS,
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
        flight.skinningSSBO = _render->getResourceFactory()->createBuffer(BufferCreateInfo{
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

void DirectionalShadowPass::setDepthAttachment(stdptr<IImage> image, stdptr<IImageView> view)
{
    _depthImage = std::move(image);
    _depthView  = std::move(view);
}

} // namespace ya
