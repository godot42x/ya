#include "Render3D/WorldDraw.h"

#include "Core/Common/DeferredDeletionQueue.h"
#include "Core/Log.h"
#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/RenderResourceFactory.h"
#include "RHI/Render.h"

#include <algorithm>
#include <cstring>
#include <format>

namespace ya
{

namespace
{

ViewportState worldViewportState()
{
    return ViewportState{
        .viewports = {Viewport{.x = 0.0f, .y = 0.0f, .width = 1.0f, .height = 1.0f, .minDepth = 0.0f, .maxDepth = 1.0f}},
        .scissors  = {Scissor{.offsetX = 0, .offsetY = 0, .width = 1, .height = 1}},
    };
}

void setWorldViewport(ICommandBuffer& cmdBuf, IRender* render, uint32_t width, uint32_t height)
{
    float viewportY      = 0.0f;
    float viewportHeight = static_cast<float>(height);
    if (render && render->getAPI() == ERenderAPI::Vulkan) {
        viewportY      = static_cast<float>(height);
        viewportHeight = -static_cast<float>(height);
    }
    cmdBuf.setViewport(0.0f, viewportY, static_cast<float>(width), viewportHeight, 0.0f, 1.0f);
    cmdBuf.setScissor(0, 0, width, height);
}

GraphicsPipelineCreateInfo buildWorldLinePipeline(IPipelineLayout* layout,
                                                  const std::string& label,
                                                  EFormat::T colorFormat,
                                                  EFormat::T depthFormat)
{
    const bool bDepth = depthFormat != EFormat::Undefined;
    return GraphicsPipelineCreateInfo{
        .subPassRef            = 0,
        .renderPass            = nullptr,
        .pipelineRenderingInfo = PipelineRenderingInfo{
            .label                  = label,
            .viewMask               = 0,
            .colorAttachmentFormats = {colorFormat},
            .depthAttachmentFormat  = depthFormat,
        },
        .pipelineLayout = layout,
        .shaderDesc = ShaderDesc{
            .sourceMode = ShaderDesc::ESourceMode::StageFiles,
            .stageFiles = {
                ShaderDesc::StageFile{.stage = EShaderStage::Vertex, .file = "Sprite2DLine.slang", .entryName = "vertLineMain"},
                ShaderDesc::StageFile{.stage = EShaderStage::Fragment, .file = "Sprite2DLine.slang", .entryName = "fragLineMain"},
            },
            .vertexBufferDescs = {VertexBufferDescription{.slot = 0, .pitch = sizeof(WorldDrawVertex)}},
            .vertexAttributes = {
                VertexAttribute{.bufferSlot = 0, .location = 0, .format = EVertexAttributeFormat::Float3, .offset = offsetof(WorldDrawVertex, pos)},
                VertexAttribute{.bufferSlot = 0, .location = 1, .format = EVertexAttributeFormat::Float4, .offset = offsetof(WorldDrawVertex, color)},
            },
        },
        .dynamicFeatures = {EPipelineDynamicFeature::Viewport, EPipelineDynamicFeature::Scissor},
        .primitiveType   = EPrimitiveType::Line,
        .rasterizationState = RasterizationState{
            .polygonMode = EPolygonMode::Fill,
            .cullMode    = ECullMode::None,
            .frontFace   = EFrontFaceType::CounterClockWise,
        },
        .multisampleState  = MultisampleState{},
        .depthStencilState = DepthStencilState{
            .bDepthTestEnable       = bDepth,
            .bDepthWriteEnable      = false,
            .depthCompareOp         = bDepth ? ECompareOp::LessOrEqual : ECompareOp::Always,
            .bDepthBoundsTestEnable = false,
            .bStencilTestEnable     = false,
            .minDepthBounds         = 0.0f,
            .maxDepthBounds         = 1.0f,
        },
        .colorBlendState = ColorBlendState{
            .bLogicOpEnable = false,
            .attachments    = {ColorBlendAttachmentState{
                .index               = 0,
                .bBlendEnable        = true,
                .srcColorBlendFactor = EBlendFactor::SrcAlpha,
                .dstColorBlendFactor = EBlendFactor::OneMinusSrcAlpha,
                .colorBlendOp        = EBlendOp::Add,
                .srcAlphaBlendFactor = EBlendFactor::One,
                .dstAlphaBlendFactor = EBlendFactor::Zero,
                .alphaBlendOp        = EBlendOp::Add,
                .colorWriteMask      = static_cast<EColorComponent::T>(EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A),
            }},
        },
        .viewportState = worldViewportState(),
    };
}

} // namespace

void WorldDrawPipelines::init(IRender* render)
{
    _render = render;
    DescriptorSetLayoutDesc frameLayout{
        .label    = "Frame_UBO",
        .set      = 0,
        .bindings = {DescriptorSetLayoutBinding{
            .binding         = 0,
            .descriptorType  = EPipelineDescriptorType::UniformBuffer,
            .descriptorCount = 1,
            .stageFlags      = EShaderStage::Vertex,
        }},
    };
    _frameUboDSL = IDescriptorSetLayout::create(render, frameLayout);
    _pipelineLayout = IPipelineLayout::create(render, "Sprite2D_Line_PipelineLayout", {}, {_frameUboDSL});
}

void WorldDrawPipelines::destroy()
{
    for (auto& variant : _variants) {
        DeferredDeletionQueue::get().retire(std::move(variant.pipeline));
    }
    _variants.clear();
    auto& queue = DeferredDeletionQueue::get();
    queue.retire(std::move(_pipelineLayout));
    queue.retire(std::move(_frameUboDSL));
    _render = nullptr;
}

IGraphicsPipeline* WorldDrawPipelines::prepare(EFormat::T colorFormat, EFormat::T depthFormat)
{
    if (!_render || colorFormat == EFormat::Undefined) {
        return nullptr;
    }
    for (auto& variant : _variants) {
        if (variant.color == colorFormat && variant.depth == depthFormat && variant.pipeline) {
            return variant.pipeline.get();
        }
    }
    auto pipeline = IGraphicsPipeline::create(_render);
    pipeline->recreate(buildWorldLinePipeline(
        _pipelineLayout.get(),
        depthFormat == EFormat::Undefined ? "Sprite2D_Line_UI_Pipeline" : "Sprite2D_Line_Screen_Pipeline",
        colorFormat,
        depthFormat));
    _variants.push_back(Variant{.color = colorFormat, .depth = depthFormat, .pipeline = std::move(pipeline)});
    return _variants.back().pipeline.get();
}

void WorldDrawRecorder::init(WorldDrawPipelines& pipelines)
{
    _pipelines = &pipelines;
    _render = pipelines.render();
    YA_CORE_ASSERT(_render != nullptr, "WorldDrawRecorder requires an initialized pipeline cache");
    _descriptorPool = IDescriptorPool::create(
        _render,
        DescriptorPoolCreateInfo{
            .maxSets   = MAX_FLIGHTS_IN_FLIGHT,
            .poolSizes = {DescriptorPoolSize{
                .type            = EPipelineDescriptorType::UniformBuffer,
                .descriptorCount = MAX_FLIGHTS_IN_FLIGHT,
            }},
        });
    ensureResources();
}

void WorldDrawRecorder::destroy()
{
    auto& queue = DeferredDeletionQueue::get();
    for (auto& flight : _flights) {
        queue.retire(std::move(flight.vertexBuffer));
        queue.retire(std::move(flight.frameUBOBuffer));
        flight.vertexPtrHead = nullptr;
        flight.frameUboDS = {};
    }
    queue.retire(std::move(_descriptorPool));
    _pipeline = nullptr;
    _pipelines = nullptr;
    _render = nullptr;
}

void WorldDrawRecorder::prepare(EFormat::T colorFormat, EFormat::T depthFormat)
{
    YA_CORE_ASSERT(_pipelines != nullptr, "WorldDrawRecorder::prepare before init");
    _colorFormat = colorFormat;
    _depthFormat = depthFormat;
    _pipeline = _pipelines->prepare(colorFormat, depthFormat);
}

void WorldDrawRecorder::ensureResources()
{
    if (_flights[0].frameUBOBuffer) {
        return;
    }
    std::vector<DescriptorSetHandle> sets;
    _descriptorPool->allocateDescriptorSets(_pipelines->_frameUboDSL, MAX_FLIGHTS_IN_FLIGHT, sets);
    for (uint32_t flight = 0; flight < MAX_FLIGHTS_IN_FLIGHT; ++flight) {
        auto& resources = _flights[flight];
        resources.frameUboDS = sets[flight];
        resources.frameUBOBuffer = _render->getResourceFactory()->createBuffer(BufferCreateInfo{
            .label       = std::format("Sprite2D_Line_{}_FrameUBO", flight),
            .usage       = EBufferUsage::UniformBuffer,
            .size        = sizeof(FrameUBO),
            .memoryUsage = EMemoryUsage::CpuToGpu,
        });
        resources.vertexBuffer = _render->getResourceFactory()->createBuffer(BufferCreateInfo{
            .label       = std::format("Sprite2D_Line_{}_VertexBuffer", flight),
            .usage       = EBufferUsage::VertexBuffer | EBufferUsage::TransferDst,
            .size        = sizeof(WorldDrawVertex) * WorldDrawPipelines::MaxVertexCount * WorldDrawPipelines::kFrameFlushSlots,
            .memoryUsage = EMemoryUsage::CpuToGpu,
        });
        resources.vertexPtrHead = resources.vertexBuffer->map<WorldDrawVertex>();
        _render->getDescriptorHelper()->updateDescriptorSets({
            IDescriptorSetHelper::writeOneUniformBuffer(resources.frameUboDS, 0, resources.frameUBOBuffer.get()),
        });
    }
}

void WorldDrawRecorder::record(WorldDrawList& list, const WorldDrawTarget& target)
{
    if (!target.cmd || !_pipelines || list.vertices.empty()) {
        return;
    }
    list.seal();
    if (list.commands.empty()) {
        return;
    }
    YA_CORE_ASSERT(_pipeline != nullptr && _colorFormat == target.colorFormat && _depthFormat == target.depthFormat,
                   "WorldDrawRecorder::record requires prepare() for this target format");

    const uint32_t framesInFlight = std::max(1u, _render->framesInFlight());
    const uint32_t flight = static_cast<uint32_t>(_render->recordedFrameIndex() % framesInFlight) % MAX_FLIGHTS_IN_FLIGHT;
    auto& resources = _flights[flight];
    WorldDrawVertex* cursor = resources.vertexPtrHead;
    uint32_t vertexCount = 0;

    FrameUBO ubo{.viewProj = target.viewProjection, .view = glm::mat4(1.0f)};
    resources.frameUBOBuffer->writeData(&ubo, sizeof(ubo), 0);

    for (const WorldDrawList::Command& command : list.commands) {
        YA_CORE_ASSERT(static_cast<uint64_t>(vertexCount) + command.vertexCount <=
                           WorldDrawPipelines::MaxVertexCount * WorldDrawPipelines::kFrameFlushSlots,
                       "World draw frame exceeded vertex buffer capacity");
        std::memcpy(cursor, list.vertices.data() + command.firstVertex, command.vertexCount * sizeof(WorldDrawVertex));
        cursor += command.vertexCount;
        vertexCount += command.vertexCount;
    }
    resources.vertexBuffer->flush();

    target.cmd->bindPipeline(_pipeline);
    setWorldViewport(*target.cmd, _render, target.width, target.height);
    target.cmd->bindDescriptorSets(_pipelines->layout(), 0, {resources.frameUboDS});
    target.cmd->bindVertexBuffer(0, resources.vertexBuffer.get(), 0);
    target.cmd->draw(vertexCount, 1, 0, 0);
}

} // namespace ya
