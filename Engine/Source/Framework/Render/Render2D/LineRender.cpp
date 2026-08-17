#include "Draw2DInternal.h"
#include "Render2D.h"

#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/RenderPass.h"
#include "RHI/Core/RenderResourceFactory.h"
#include "RHI/Render.h"
#include "RHI/RenderDefines.h"

#include "Core/Math/GLM.h"

#include <glm/gtc/constants.hpp>

#include <format>

namespace ya
{

void FLineRender::init(IRender* render, EFormat::T colorFormat, EFormat::T depthFormat)
{
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

    const auto buildVertexAttributes = []()
    {
        return std::vector<VertexAttribute>{
            VertexAttribute{
                .bufferSlot = 0,
                .location   = 0,
                .format     = EVertexAttributeFormat::Float3,
                .offset     = offsetof(Vertex, pos),
            },
            VertexAttribute{
                .bufferSlot = 0,
                .location   = 1,
                .format     = EVertexAttributeFormat::Float4,
                .offset     = offsetof(Vertex, color),
            },
        };
    };

    _pipeline = IGraphicsPipeline::create(render);
    _pipeline->recreate(GraphicsPipelineCreateInfo{
        .subPassRef            = 0,
        .renderPass            = nullptr,
        .pipelineRenderingInfo = PipelineRenderingInfo{
            .label                  = "Sprite2D_Line_Pipeline",
            .viewMask               = 0,
            .colorAttachmentFormats = {colorFormat},
            .depthAttachmentFormat  = depthFormat,
        },
        .pipelineLayout = _pipelineLayout.get(),
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
            .vertexAttributes = buildVertexAttributes(),
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
            // Depth-test debug lines against the scene depth (the composition
            // pass attaches the viewport depth buffer). Lines never write depth.
            .bDepthTestEnable       = true,
            .bDepthWriteEnable      = false,
            .depthCompareOp         = ECompareOp::LessOrEqual,
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
    });
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
    _pipeline.reset();
    _pipelineLayout.reset();
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
    }
}

void FLineRender::begin(Render2DPassSlot passSlot)
{
    _activePassSlot  = passSlot;
    _activeFlightIndex = _render ? _render->getCurrentFrameIndex() % MAX_FLIGHTS_IN_FLIGHT : 0;
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

    DescriptorBufferInfo bufferInfo(
        BufferHandle(resources.frameUBOBuffer->getHandle()),
        0,
        static_cast<uint64_t>(sizeof(FrameUBO)));
    _render->getDescriptorHelper()->updateDescriptorSets(
        {
            IDescriptorSetHelper::genBufferWrite(resources.frameUboDS,
                                                 0,
                                                 0,
                                                 EPipelineDescriptorType::UniformBuffer,
                                                 {bufferInfo}),
        },
        {});
    resources.vertexBuffer->flush();

    cmdBuf->bindPipeline(_pipeline.get());
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
