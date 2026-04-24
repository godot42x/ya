#include "DebugPrimitives.h"

#include "Core/Math/Math.h"
#include "Resource/Mesh/PrimitiveMeshCache.h"

#include "ImGuiHelper.h"

#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>

namespace ya
{

namespace
{

constexpr std::array<VertexAttribute, 2> DEBUG_LINE_ATTRIBUTES = {
    VertexAttribute{.bufferSlot = 0, .location = 0, .format = EVertexAttributeFormat::Float3, .offset = offsetof(DebugPrimitives::LineVertex, position)},
    VertexAttribute{.bufferSlot = 0, .location = 1, .format = EVertexAttributeFormat::Float4, .offset = offsetof(DebugPrimitives::LineVertex, color)},
};

} // namespace

void DebugPrimitives::init(IRender* render)
{
    _render = render;
    initFrameResources();
    initLinePipeline();
    initShapePipeline();
    ensureLineBufferCapacity(MAX_LINE_VERTICES);
}

void DebugPrimitives::destroy()
{
    for (auto& ubo : _frameUBO) {
        ubo.reset();
    }
    _frameDSP.reset();
    _frameDSL.reset();
    _linePipeline.reset();
    _linePipelineLayout.reset();
    _shapePipeline.reset();
    _shapePipelineLayout.reset();
    _lineVertexBuffer.reset();
    _lineVertexCapacity = 0;
    clear();
    _render = nullptr;
}

void DebugPrimitives::beginFrame()
{
    if (_linePipeline) {
        _linePipeline->beginFrame();
    }
    if (_shapePipeline) {
        _shapePipeline->beginFrame();
    }

    _frameLineVertices.swap(_queuedLineVertices);
    _frameShapeInstances.swap(_queuedShapeInstances);
    _queuedLineVertices.clear();
    _queuedShapeInstances.clear();

    clearImmediate();
}

void DebugPrimitives::clear()
{
    _queuedLineVertices.clear();
    _queuedShapeInstances.clear();
    _frameLineVertices.clear();
    _frameShapeInstances.clear();
    clearImmediate();
}

void DebugPrimitives::refreshPipelineFormats(const IRenderTarget* viewportRT)
{
    applyPipelineFormats(_linePipeline, viewportRT);
    applyPipelineFormats(_shapePipeline, viewportRT);
    updateDepthState();
}

void DebugPrimitives::addLine(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color)
{
    _queuedLineVertices.push_back(LineVertex{.position = from, .color = color});
    _queuedLineVertices.push_back(LineVertex{.position = to, .color = color});
}

void DebugPrimitives::addSphere(const glm::vec3& center, float radius, const glm::vec4& color)
{
    _queuedShapeInstances.push_back(ShapeInstance{
        .model     = buildSphereModel(center, radius),
        .color     = color,
        .primitive = EPrimitiveGeometry::Sphere,
    });
}

void DebugPrimitives::addCylinder(const glm::mat4& model, const glm::vec4& color)
{
    _queuedShapeInstances.push_back(ShapeInstance{
        .model     = model,
        .color     = color,
        .primitive = EPrimitiveGeometry::Cylinder,
    });
}

void DebugPrimitives::addCone(const glm::mat4& model, const glm::vec4& color)
{
    _queuedShapeInstances.push_back(ShapeInstance{
        .model     = model,
        .color     = color,
        .primitive = EPrimitiveGeometry::Cone,
    });
}

void DebugPrimitives::addLineImmediate(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color)
{
    _immediateLineVertices.push_back(LineVertex{.position = from, .color = color});
    _immediateLineVertices.push_back(LineVertex{.position = to, .color = color});
}

void DebugPrimitives::addSphereImmediate(const glm::vec3& center, float radius, const glm::vec4& color)
{
    _immediateShapeInstances.push_back(ShapeInstance{
        .model     = buildSphereModel(center, radius),
        .color     = color,
        .primitive = EPrimitiveGeometry::Sphere,
    });
}

void DebugPrimitives::addCylinderImmediate(const glm::mat4& model, const glm::vec4& color)
{
    _immediateShapeInstances.push_back(ShapeInstance{
        .model     = model,
        .color     = color,
        .primitive = EPrimitiveGeometry::Cylinder,
    });
}

void DebugPrimitives::addConeImmediate(const glm::mat4& model, const glm::vec4& color)
{
    _immediateShapeInstances.push_back(ShapeInstance{
        .model     = model,
        .color     = color,
        .primitive = EPrimitiveGeometry::Cone,
    });
}

void DebugPrimitives::draw(ICommandBuffer*  cmdBuf,
                           uint32_t         viewportWidth,
                           uint32_t         viewportHeight,
                           const glm::mat4& projection,
                           const glm::mat4& view)
{
    if (!bEnabled || !cmdBuf || viewportWidth == 0 || viewportHeight == 0) {
        return;
    }

    updateDepthState();
    _frameData.projection = projection;
    _frameData.view       = view;
    updateFrameUBO();
    const uint32_t flightIndex = _render ? _render->getCurrentFrameIndex() % MAX_FLIGHTS_IN_FLIGHT : 0;

    if (bDrawLines) {
        if (!_frameLineVertices.empty()) {
            drawLines(cmdBuf, viewportWidth, viewportHeight, flightIndex, _frameLineVertices);
        }
        if (!_immediateLineVertices.empty()) {
            drawLines(cmdBuf, viewportWidth, viewportHeight, flightIndex, _immediateLineVertices);
        }
    }
    if (bDrawShapes) {
        if (!_frameShapeInstances.empty()) {
            drawShapes(cmdBuf, viewportWidth, viewportHeight, flightIndex, _frameShapeInstances);
        }
        if (!_immediateShapeInstances.empty()) {
            drawShapes(cmdBuf, viewportWidth, viewportHeight, flightIndex, _immediateShapeInstances);
        }
    }
}

void DebugPrimitives::renderGUI()
{
    if (!ImGui::TreeNode("Debug Primitives")) {
        return;
    }

    ImGui::Checkbox("Enabled", &bEnabled);
    ImGui::Checkbox("Depth Test", &bDepthTest);
    ImGui::Checkbox("Draw Lines", &bDrawLines);
    ImGui::Checkbox("Draw Shapes", &bDrawShapes);
    size_t pendingLineCount  = _queuedLineVertices.size() / 2;
    size_t pendingShapeCount = _queuedShapeInstances.size();
    ImGui::Text("Pending Lines: %d", static_cast<int>(pendingLineCount));
    ImGui::Text("Pending Shapes: %d", static_cast<int>(pendingShapeCount));
    ImGui::Text("Frame Lines: %d", static_cast<int>(_frameLineVertices.size() / 2));
    ImGui::Text("Frame Shapes: %d", static_cast<int>(_frameShapeInstances.size()));
    ImGui::Text("Immediate Lines: %d", static_cast<int>(_immediateLineVertices.size() / 2));
    ImGui::Text("Immediate Shapes: %d", static_cast<int>(_immediateShapeInstances.size()));
    if (_linePipeline) {
        _linePipeline->renderGUI();
    }
    if (_shapePipeline) {
        _shapePipeline->renderGUI();
    }

    ImGui::TreePop();
}
void DebugPrimitives::initFrameResources()
{
    _frameDSL = IDescriptorSetLayout::create(_render, DescriptorSetLayoutDesc{
                                                          .label    = "DebugPrimitive_Frame_DSL",
                                                          .set      = 0,
                                                          .bindings = {{.binding = 0,
                                                                        .descriptorType = EPipelineDescriptorType::UniformBuffer,
                                                                        .descriptorCount = 1,
                                                                        .stageFlags = EShaderStage::Vertex}},
                                                      });

    _frameDSP = IDescriptorPool::create(_render, DescriptorPoolCreateInfo{
                                                     .label     = "DebugPrimitive_Frame_DSP",
                                                     .maxSets   = MAX_FLIGHTS_IN_FLIGHT,
                                                     .poolSizes = {{.type = EPipelineDescriptorType::UniformBuffer,
                                                                    .descriptorCount = MAX_FLIGHTS_IN_FLIGHT}},
                                                 });

    FrameUBO initialData{};
    for (uint32_t i = 0; i < MAX_FLIGHTS_IN_FLIGHT; ++i) {
        _frameUBO[i] = IBuffer::create(_render, BufferCreateInfo{
                                                   .label       = std::format("DebugPrimitive_Frame_UBO_{}", i),
                                                   .usage       = EBufferUsage::UniformBuffer,
                                                   .size        = sizeof(FrameUBO),
                                                   .memoryUsage = EMemoryUsage::CpuToGpu,
                                               });
        _frameUBO[i]->writeData(&initialData, sizeof(FrameUBO), 0);

        _frameDS[i] = _frameDSP->allocateDescriptorSets(_frameDSL);
        _render->getDescriptorHelper()->updateDescriptorSets({
            IDescriptorSetHelper::writeOneUniformBuffer(_frameDS[i], 0, _frameUBO[i].get()),
        });
    }
}

void DebugPrimitives::initLinePipeline()
{
    _linePipelineLayout = IPipelineLayout::create(_render, "DebugPrimitiveLine_PPL", {}, {_frameDSL});

    GraphicsPipelineCreateInfo ci{};
    ci.renderPass = nullptr;
    ci.shaderDesc = {
        .shaderName        = "Test/DebugPrimitiveLine.glsl",
        .vertexBufferDescs = {VertexBufferDescription{.slot = 0, .pitch = sizeof(LineVertex)}},
        .vertexAttributes  = {DEBUG_LINE_ATTRIBUTES.begin(), DEBUG_LINE_ATTRIBUTES.end()},
    };
    ci.pipelineRenderingInfo = {
        .label                  = "DebugPrimitiveLinePipeline",
        .colorAttachmentFormats = {LINEAR_FORMAT},
        .depthAttachmentFormat  = DEPTH_FORMAT,
    };
    ci.pipelineLayout     = _linePipelineLayout.get();
    ci.dynamicFeatures    = {EPipelineDynamicFeature::Viewport, EPipelineDynamicFeature::Scissor};
    ci.primitiveType      = EPrimitiveType::Line;
    ci.rasterizationState = {.polygonMode = EPolygonMode::Fill, .cullMode = ECullMode::None, .frontFace = EFrontFaceType::CounterClockWise};
    ci.depthStencilState  = {.bDepthTestEnable = true, .bDepthWriteEnable = false, .depthCompareOp = ECompareOp::LessOrEqual};
    ci.colorBlendState    = {.attachments = {{.index = 0, .bBlendEnable = true, .srcColorBlendFactor = EBlendFactor::SrcAlpha, .dstColorBlendFactor = EBlendFactor::OneMinusSrcAlpha, .colorBlendOp = EBlendOp::Add, .srcAlphaBlendFactor = EBlendFactor::SrcAlpha, .dstAlphaBlendFactor = EBlendFactor::OneMinusSrcAlpha, .alphaBlendOp = EBlendOp::Add, .colorWriteMask = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A}}};
    ci.viewportState      = {.viewports = {Viewport::defaults()}, .scissors = {Scissor::defaults()}};

    _linePipeline = IGraphicsPipeline::create(_render);
    YA_CORE_ASSERT(_linePipeline && _linePipeline->recreate(ci), "Failed to create debug primitive line pipeline");
}

void DebugPrimitives::initShapePipeline()
{
    constexpr auto pcSize = sizeof(ShapePushConstant);
    _shapePipelineLayout  = IPipelineLayout::create(
        _render,
        "DebugPrimitiveShape_PPL",
        {PushConstantRange{.offset = 0, .size = pcSize, .stageFlags = EShaderStage::Vertex | EShaderStage::Fragment}},
        {_frameDSL});

    GraphicsPipelineCreateInfo ci{};
    ci.renderPass = nullptr;
    ci.shaderDesc = {
        .shaderName        = "Test/DebugPrimitiveShape.glsl",
        .vertexBufferDescs = {VertexBufferDescription{.slot = 0, .pitch = sizeof(ya::Vertex)}},
        .vertexAttributes  = {
            {.bufferSlot = 0, .location = 0, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, position)},
        },
    };
    ci.pipelineRenderingInfo = {
        .label                  = "DebugPrimitiveShapePipeline",
        .colorAttachmentFormats = {LINEAR_FORMAT},
        .depthAttachmentFormat  = DEPTH_FORMAT,
    };
    ci.pipelineLayout     = _shapePipelineLayout.get();
    ci.dynamicFeatures    = {EPipelineDynamicFeature::Viewport, EPipelineDynamicFeature::Scissor};
    ci.primitiveType      = EPrimitiveType::TriangleList;
    ci.rasterizationState = {.polygonMode = EPolygonMode::Line, .cullMode = ECullMode::None, .frontFace = EFrontFaceType::CounterClockWise};
    ci.depthStencilState  = {.bDepthTestEnable = true, .bDepthWriteEnable = false, .depthCompareOp = ECompareOp::LessOrEqual};
    ci.colorBlendState    = {.attachments = {{.index = 0, .bBlendEnable = true, .srcColorBlendFactor = EBlendFactor::SrcAlpha, .dstColorBlendFactor = EBlendFactor::OneMinusSrcAlpha, .colorBlendOp = EBlendOp::Add, .srcAlphaBlendFactor = EBlendFactor::SrcAlpha, .dstAlphaBlendFactor = EBlendFactor::OneMinusSrcAlpha, .alphaBlendOp = EBlendOp::Add, .colorWriteMask = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A}}};
    ci.viewportState      = {.viewports = {Viewport::defaults()}, .scissors = {Scissor::defaults()}};

    _shapePipeline = IGraphicsPipeline::create(_render);
    YA_CORE_ASSERT(_shapePipeline && _shapePipeline->recreate(ci), "Failed to create debug primitive shape pipeline");
}

void DebugPrimitives::ensureLineBufferCapacity(uint32_t requiredVertexCount)
{
    if (_lineVertexBuffer && _lineVertexCapacity >= requiredVertexCount) {
        return;
    }

    _lineVertexCapacity = std::max(requiredVertexCount, MAX_LINE_VERTICES);
    _lineVertexBuffer   = IBuffer::create(_render, BufferCreateInfo{
        .label       = "DebugPrimitiveLineVertexBuffer",
        .usage       = EBufferUsage::VertexBuffer,
        .size        = static_cast<uint32_t>(sizeof(LineVertex) * _lineVertexCapacity),
        .memoryUsage = EMemoryUsage::CpuToGpu,
    });
    YA_CORE_ASSERT(_lineVertexBuffer, "Failed to create debug primitive line vertex buffer");
}

void DebugPrimitives::updateFrameUBO()
{
    if (!_render) {
        return;
    }

    const uint32_t flightIndex = _render->getCurrentFrameIndex() % MAX_FLIGHTS_IN_FLIGHT;
    YA_CORE_ASSERT(flightIndex < MAX_FLIGHTS_IN_FLIGHT, "Invalid debug primitive flight index");
    YA_CORE_ASSERT(_frameUBO[flightIndex], "Missing debug primitive frame UBO");
    _frameUBO[flightIndex]->writeData(&_frameData, sizeof(FrameUBO), 0);
}

void DebugPrimitives::applyPipelineFormats(stdptr<IGraphicsPipeline>& pipeline, const IRenderTarget* viewportRT)
{
    if (!pipeline || !viewportRT) {
        return;
    }

    const auto& colorDescs = viewportRT->getColorAttachmentDescs();
    const auto& depthDesc  = viewportRT->getDepthAttachmentDesc();
    if (colorDescs.empty()) {
        return;
    }

    auto ci                                         = pipeline->getDesc();
    ci.pipelineRenderingInfo.colorAttachmentFormats = {colorDescs.front().format};
    ci.pipelineRenderingInfo.depthAttachmentFormat  = depthDesc.has_value() ? depthDesc->format : EFormat::Undefined;
    pipeline->updateDesc(std::move(ci));
}

void DebugPrimitives::updateDepthState()
{
    auto apply = [this](stdptr<IGraphicsPipeline>& pipeline)
    {
        if (!pipeline) {
            return;
        }

        auto ci                                   = pipeline->getDesc();
        ci.depthStencilState.bDepthTestEnable     = bDepthTest;
        ci.depthStencilState.bDepthWriteEnable    = false;
        ci.depthStencilState.depthCompareOp       = ECompareOp::LessOrEqual;
        pipeline->updateDesc(std::move(ci));
    };

    apply(_linePipeline);
    apply(_shapePipeline);
}

void DebugPrimitives::clearImmediate()
{
    _immediateLineVertices.clear();
    _immediateShapeInstances.clear();
}

void DebugPrimitives::setViewportAndScissor(ICommandBuffer* cmdBuf, uint32_t viewportWidth, uint32_t viewportHeight) const
{
    float viewportY      = 0.0f;
    float viewportHeightSigned = static_cast<float>(viewportHeight);
    if (bReverseViewportY) {
        viewportY             = static_cast<float>(viewportHeight);
        viewportHeightSigned  = -viewportHeightSigned;
    }

    cmdBuf->setViewport(0.0f, viewportY, static_cast<float>(viewportWidth), viewportHeightSigned, 0.0f, 1.0f);
    cmdBuf->setScissor(0, 0, viewportWidth, viewportHeight);
}

void DebugPrimitives::drawLines(ICommandBuffer*                cmdBuf,
                                uint32_t                       viewportWidth,
                                uint32_t                       viewportHeight,
                                uint32_t                       flightIndex,
                                const std::vector<LineVertex>& vertices)
{
    ensureLineBufferCapacity(static_cast<uint32_t>(vertices.size()));
    _lineVertexBuffer->writeData(vertices.data(), static_cast<uint32_t>(sizeof(LineVertex) * vertices.size()), 0);

    cmdBuf->bindPipeline(_linePipeline.get());
    setViewportAndScissor(cmdBuf, viewportWidth, viewportHeight);
    cmdBuf->bindDescriptorSets(_linePipelineLayout.get(), 0, {_frameDS[flightIndex]});
    cmdBuf->bindVertexBuffer(0, _lineVertexBuffer.get(), 0);
    cmdBuf->draw(static_cast<uint32_t>(vertices.size()));
}

void DebugPrimitives::drawShapes(ICommandBuffer*  cmdBuf,
                                 uint32_t         viewportWidth,
                                 uint32_t         viewportHeight,
                                 uint32_t         flightIndex,
                                 const std::vector<ShapeInstance>& shapes)
{
    cmdBuf->bindPipeline(_shapePipeline.get());
    setViewportAndScissor(cmdBuf, viewportWidth, viewportHeight);
    cmdBuf->bindDescriptorSets(_shapePipelineLayout.get(), 0, {_frameDS[flightIndex]});

    for (const auto& instance : shapes) {
        Mesh* mesh = PrimitiveMeshCache::get().getMesh(instance.primitive);
        if (!mesh) {
            continue;
        }

        ShapePushConstant pc{
            .model = instance.model,
            .color = instance.color,
        };
        cmdBuf->pushConstants(_shapePipelineLayout.get(), EShaderStage::Vertex | EShaderStage::Fragment, 0, sizeof(ShapePushConstant), &pc);
        mesh->draw(cmdBuf);
    }
}

glm::mat4 DebugPrimitives::buildSphereModel(const glm::vec3& center, float radius)
{
    return glm::translate(glm::mat4(1.0f), center) *
           glm::scale(glm::mat4(1.0f), glm::vec3(radius));
}

} // namespace ya
