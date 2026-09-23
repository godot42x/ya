#include "Draw2DInternal.h"
#include "Render2D/Render2D.h"

#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/Pipeline.h"
#include "RHI/Core/RenderPass.h"
#include "RHI/Core/RenderResourceFactory.h"
#include "RHI/Render.h"
#include "RHI/RenderDefines.h"

#include "Core/Common/DeferredDeletionQueue.h"
#include "Core/Log.h"
#include "Core/Math/GLM.h"

#include <glm/gtc/constants.hpp>

#include <format>
#include <string>

namespace ya
{

namespace
{

std::vector<VertexAttribute> buildLineVertexAttributes()
{
    return std::vector<VertexAttribute>{
        VertexAttribute{
            .bufferSlot = 0,
            .location   = 0,
            .format     = EVertexAttributeFormat::Float3,
            .offset     = offsetof(FLineRender::Vertex, pos),
        },
        VertexAttribute{
            .bufferSlot = 0,
            .location   = 1,
            .format     = EVertexAttributeFormat::Float4,
            .offset     = offsetof(FLineRender::Vertex, color),
        },
    };
}

GraphicsPipelineCreateInfo buildLinePipelineCI(IPipelineLayout* pipelineLayout,
                                               const std::string& label,
                                               EFormat::T colorFormat,
                                               EFormat::T depthFormat)
{
    const bool bDepthAttached = depthFormat != EFormat::Undefined;
    return GraphicsPipelineCreateInfo{
        .subPassRef            = 0,
        .renderPass            = nullptr,
        .pipelineRenderingInfo = PipelineRenderingInfo{
            .label                  = label,
            .viewMask               = 0,
            .colorAttachmentFormats = {colorFormat},
            .depthAttachmentFormat  = depthFormat,
        },
        .pipelineLayout = pipelineLayout,
        .shaderDesc = ShaderDesc{
            .sourceMode        = ShaderDesc::ESourceMode::StageFiles,
            .stageFiles        = {
                ShaderDesc::StageFile{.stage = EShaderStage::Vertex, .file = "Sprite2DLine.slang", .entryName = "vertLineMain"},
                ShaderDesc::StageFile{.stage = EShaderStage::Fragment, .file = "Sprite2DLine.slang", .entryName = "fragLineMain"},
            },
            .vertexBufferDescs = {
                VertexBufferDescription{
                    .slot  = 0,
                    .pitch = sizeof(FLineRender::Vertex),
                },
            },
            .vertexAttributes = buildLineVertexAttributes(),
        },
        .dynamicFeatures = {
            EPipelineDynamicFeature::Viewport,
            EPipelineDynamicFeature::Scissor,
        },
        .primitiveType      = EPrimitiveType::Line,
        .rasterizationState = RasterizationState{
            .polygonMode = EPolygonMode::Fill,
            .cullMode    = ECullMode::None,
            .frontFace   = EFrontFaceType::CounterClockWise,
        },
        .multisampleState  = MultisampleState{},
        .depthStencilState = DepthStencilState{
            // Depth-attached compose (overlay / editor viewport) tests debug
            // lines against scene depth and never writes it. Depth-less UI
            // composite must not enable depth test: that slot has no depth
            // attachment, and must not overwrite the depth-aware variant.
            .bDepthTestEnable       = bDepthAttached,
            .bDepthWriteEnable      = false,
            .depthCompareOp         = bDepthAttached ? ECompareOp::LessOrEqual : ECompareOp::Always,
            .bDepthBoundsTestEnable = false,
            .bStencilTestEnable     = false,
            .minDepthBounds         = 0.0f,
            .maxDepthBounds         = 1.0f,
        },
        .colorBlendState = ColorBlendState{
            .bLogicOpEnable = false,
            .attachments    = {
                ColorBlendAttachmentState{
                    .index               = 0,
                    .bBlendEnable        = true,
                    .srcColorBlendFactor = EBlendFactor::SrcAlpha,
                    .dstColorBlendFactor = EBlendFactor::OneMinusSrcAlpha,
                    .colorBlendOp        = EBlendOp::Add,
                    .srcAlphaBlendFactor = EBlendFactor::One,
                    .dstAlphaBlendFactor = EBlendFactor::Zero,
                    .alphaBlendOp        = EBlendOp::Add,
                    .colorWriteMask      = static_cast<EColorComponent::T>(EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A),
                },
            },
        },
        .viewportState = buildQuadViewportState(),
    };
}

} // namespace

void FLineRender::init(IRender* render, EFormat::T colorFormat, EFormat::T depthFormat)
{
    (void)colorFormat;
    (void)depthFormat;
    _render = render;
    constexpr uint32_t resourceCount = FQuadRender::kMaxPassSlots * MAX_FLIGHTS_IN_FLIGHT;

    _descriptorPool = IDescriptorPool::create(
        render,
        DescriptorPoolCreateInfo{
            .maxSets   = resourceCount,
            .poolSizes = {
                DescriptorPoolSize{
                    .type            = EPipelineDescriptorType::UniformBuffer,
                    .descriptorCount = resourceCount,
                },
            },
        });

    _frameUboDSL = IDescriptorSetLayout::create(render, _pipelineDesc.descriptorSetLayouts[0]);
    std::vector<std::shared_ptr<IDescriptorSetLayout>> dslVec = {_frameUboDSL};
    _pipelineLayout = IPipelineLayout::create(render, "Sprite2D_Line_PipelineLayout", _pipelineDesc.pushConstants, dslVec);
    // Per-slot pipelines are created by preparePassPipeline. Color/depth here
    // only described the old global pipeline; they are unused on purpose.
}

void FLineRender::destroy()
{
    for (auto& pass : _passResources) {
        for (auto& resources : pass.flights) {
            resources.vertexBuffer.reset();
            resources.vertexPtrHead = nullptr;
            resources.frameUBOBuffer.reset();
            resources.frameUboDS = {};
        }
    }
    vertexPtr     = nullptr;
    vertexPtrHead = nullptr;
    _frameUboDSL.reset();
    _descriptorPool.reset();
    for (auto& pipelines : _passPipelines) {
        pipelines.screenPipeline.reset();
        pipelines.screenColorFormat = EFormat::Undefined;
        pipelines.screenDepthFormat = EFormat::Undefined;
        pipelines.uiPipeline.reset();
        pipelines.uiColorFormat = EFormat::Undefined;
    }
    _pipelineLayout.reset();
}

void FLineRender::preparePassPipeline(Render2DPassSlot passSlot, EFormat::T colorFormat, EFormat::T depthFormat)
{
    if (!_render || colorFormat == EFormat::Undefined) {
        return;
    }

    auto& pipelines = _passPipelines[static_cast<size_t>(passSlot)];
    if (depthFormat == EFormat::Undefined) {
        if (pipelines.uiPipeline && pipelines.uiColorFormat == colorFormat) {
            return;
        }

        auto pipeline = IGraphicsPipeline::create(_render);
        pipeline->recreate(buildLinePipelineCI(_pipelineLayout.get(),
                                               std::format("Sprite2D_Line_{}_UI_Pipeline", passSlot),
                                               colorFormat,
                                               EFormat::Undefined));
        auto retired = std::move(pipelines.uiPipeline);
        pipelines.uiPipeline = std::move(pipeline);
        pipelines.uiColorFormat = colorFormat;
        DeferredDeletionQueue::get().retire(std::move(retired));
        return;
    }

    if (pipelines.screenPipeline &&
        pipelines.screenColorFormat == colorFormat &&
        pipelines.screenDepthFormat == depthFormat) {
        return;
    }

    auto pipeline = IGraphicsPipeline::create(_render);
    pipeline->recreate(buildLinePipelineCI(_pipelineLayout.get(),
                                           std::format("Sprite2D_Line_{}_Screen_Pipeline", passSlot),
                                           colorFormat,
                                           depthFormat));
    auto retired = std::move(pipelines.screenPipeline);
    pipelines.screenPipeline = std::move(pipeline);
    pipelines.screenColorFormat = colorFormat;
    pipelines.screenDepthFormat = depthFormat;
    DeferredDeletionQueue::get().retire(std::move(retired));
}

void FLineRender::ensureSlotResources(Render2DPassSlot passSlot)
{
    auto& slot = _passResources[static_cast<size_t>(passSlot)];
    if (slot.flights[0].frameUBOBuffer) {
        return; // already allocated
    }
    std::vector<DescriptorSetHandle> descriptorSets;
    _descriptorPool->allocateDescriptorSets(_frameUboDSL, MAX_FLIGHTS_IN_FLIGHT, descriptorSets);
    for (uint32_t flight = 0; flight < MAX_FLIGHTS_IN_FLIGHT; ++flight) {
        auto& resources = slot.flights[flight];
        resources.frameUboDS = descriptorSets[flight];
        resources.frameUBOBuffer = _render->getResourceFactory()->createBuffer(
            ya::BufferCreateInfo{
                .label       = std::format("Sprite2D_Line_{}_{}_FrameUBO", passSlot, flight),
                .usage       = EBufferUsage::UniformBuffer,
                .size        = sizeof(FrameUBO),
                .memoryUsage = EMemoryUsage::CpuToGpu,
            });
        resources.vertexBuffer = _render->getResourceFactory()->createBuffer(
            ya::BufferCreateInfo{
                .label       = std::format("Sprite2D_Line_{}_{}_VertexBuffer", passSlot, flight),
                .usage       = EBufferUsage::VertexBuffer | EBufferUsage::TransferDst,
                .size        = sizeof(FLineRender::Vertex) * MaxVertexCount * kFrameFlushSlots,
                .memoryUsage = EMemoryUsage::CpuToGpu,
            });
        resources.vertexPtrHead = resources.vertexBuffer->map<FLineRender::Vertex>();
        // Persistent host-visible UBO: bind once here. Flush only writeData();
        // rewriting this set after vkCmdBindDescriptorSets invalidates the
        // recording command buffer (no UPDATE_AFTER_BIND).
        _render->getDescriptorHelper()->updateDescriptorSets({
            IDescriptorSetHelper::writeOneUniformBuffer(resources.frameUboDS, 0, resources.frameUBOBuffer.get()),
        });
    }
}

void FLineRender::begin(Render2DPassSlot passSlot, uint32_t flightSlot)
{
    _activePassSlot  = passSlot;
    _activeFlightIndex = flightSlot % MAX_FLIGHTS_IN_FLIGHT;
    ensureSlotResources(passSlot);
    auto& resources    = _passResources[static_cast<size_t>(_activePassSlot)].flights[_activeFlightIndex];
    vertexPtrHead      = resources.vertexPtrHead;
    vertexPtr   = vertexPtrHead;
    vertexCount = 0;
    batchStartVertex = 0;
}

void FLineRender::flush(ICommandBuffer* cmdBuf, const glm::mat4& viewProj)
{
    if (!cmdBuf || vertexCount == 0) {
        return;
    }

    FrameUBO ubo{
        .viewProj = viewProj,
        .view     = Render2D::session.view,
    };
    auto& resources = _passResources[static_cast<size_t>(_activePassSlot)].flights[_activeFlightIndex];
    resources.frameUBOBuffer->writeData(&ubo, sizeof(ubo), 0);
    resources.vertexBuffer->flush();

    auto& pipelines = _passPipelines[static_cast<size_t>(_activePassSlot)];
    IGraphicsPipeline* pipeline = pipelines.screenPipeline ? pipelines.screenPipeline.get()
                                                           : (pipelines.uiPipeline ? pipelines.uiPipeline.get() : nullptr);
    YA_CORE_ASSERT(pipeline != nullptr,
                   "Render2D line pipeline for pass slot {} was not prepared before command recording",
                   static_cast<size_t>(_activePassSlot));
    cmdBuf->bindPipeline(pipeline);
    setScreenViewportAndScissor(*cmdBuf, _render, Render2D::session.windowWidth, Render2D::session.windowHeight);

    cmdBuf->bindDescriptorSets(_pipelineLayout.get(), 0, {resources.frameUboDS});
    cmdBuf->bindVertexBuffer(0, resources.vertexBuffer.get(), 0);
    YA_CORE_ASSERT(static_cast<uint64_t>(batchStartVertex) + vertexCount <=
                       MaxVertexCount * kFrameFlushSlots,
                   "Render2D line frame exceeded vertex buffer capacity ({} batches)",
                   kFrameFlushSlots);
    cmdBuf->draw(vertexCount, 1, batchStartVertex, 0);

    batchStartVertex = static_cast<uint32_t>(vertexPtr - vertexPtrHead);
    vertexCount = 0;
}

void FLineRender::addLine(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color)
{
    if (vertexCount + 2 > MaxVertexCount) {
        flush(Render2D::session.curCmdBuf, Render2D::session.viewProjection);
    }

    *vertexPtr++ = Vertex{.pos = from, .color = color};
    *vertexPtr++ = Vertex{.pos = to, .color = color};
    vertexCount += 2;
}

void FLineRender::addWireBox(const glm::mat4& model, const glm::vec3& halfExtent, const glm::vec4& color)
{
    static constexpr std::array<glm::vec3, 8> corners = {{
        {-1.0f, -1.0f, -1.0f}, {1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, -1.0f}, {-1.0f, 1.0f, -1.0f},
        {-1.0f, -1.0f, 1.0f},  {1.0f, -1.0f, 1.0f},  {1.0f, 1.0f, 1.0f},  {-1.0f, 1.0f, 1.0f},
    }};
    static constexpr std::array<std::array<uint8_t, 2>, 12> edges = {{
        {{0, 1}}, {{1, 2}}, {{2, 3}}, {{3, 0}},
        {{4, 5}}, {{5, 6}}, {{6, 7}}, {{7, 4}},
        {{0, 4}}, {{1, 5}}, {{2, 6}}, {{3, 7}},
    }};

    for (const auto& edge : edges) {
        const glm::vec3 from = glm::vec3(model * glm::vec4(corners[edge[0]] * halfExtent, 1.0f));
        const glm::vec3 to   = glm::vec3(model * glm::vec4(corners[edge[1]] * halfExtent, 1.0f));
        addLine(from, to, color);
    }
}

void FLineRender::addWireSphere(const glm::vec3& center, float radius, const glm::vec4& color)
{
    static constexpr int   kSegmentCount = 24;
    static constexpr float kStep         = glm::two_pi<float>() / static_cast<float>(kSegmentCount);

    const auto addRing = [&](const glm::vec3& axisA, const glm::vec3& axisB)
    {
        for (int i = 0; i < kSegmentCount; ++i) {
            const float a0 = static_cast<float>(i) * kStep;
            const float a1 = static_cast<float>(i + 1) * kStep;
            const glm::vec3 from = center + radius * (axisA * glm::cos(a0) + axisB * glm::sin(a0));
            const glm::vec3 to   = center + radius * (axisA * glm::cos(a1) + axisB * glm::sin(a1));
            addLine(from, to, color);
        }
    };

    addRing({1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}); // XY
    addRing({1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}); // XZ
    addRing({0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}); // YZ
}

} // namespace ya
