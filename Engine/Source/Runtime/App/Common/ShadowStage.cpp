#include "Runtime/App/Common/ShadowStage.h"

#include "Render/Render.h"
#include "Render/RenderFrameData.h"
#include "imgui.h"

#include <algorithm>

namespace ya
{

void ShadowStage::setRenderTarget(const stdptr<IRenderTarget>& rt)
{
    _shadowMapRT = rt;
    if (_shadowMapRT) {
        _shadowExtent = _shadowMapRT->getExtent();
    }
    refreshPipelineFromRenderTarget();
}

void ShadowStage::refreshPipelineFromRenderTarget()
{
    if (!_shadowMapRT) {
        return;
    }

    const auto& depthDesc = _shadowMapRT->getDepthAttachmentDesc();
    if (!depthDesc.has_value()) {
        return;
    }

    _pipelineCI.pipelineRenderingInfo.depthAttachmentFormat = depthDesc->format;
    if (_pipeline) {
        _pipeline->updateDesc(_pipelineCI);
    }
}

void ShadowStage::init(IRender* render)
{
    _render = render;

    _frameDSL = IDescriptorSetLayout::create(
        _render,
        {
            DescriptorSetLayoutDesc{
                .label    = "ShadowStage_Frame_DSL",
                .set      = 0,
                .bindings = {{
                    .binding         = 0,
                    .descriptorType  = EPipelineDescriptorType::UniformBuffer,
                    .descriptorCount = 1,
                    .stageFlags      = EShaderStage::Vertex | EShaderStage::Geometry | EShaderStage::Fragment,
                }},
            },
        });

    _pipelineLayout = IPipelineLayout::create(_render, "ShadowStage_PPL", {PushConstantRange{.offset = 0, .size = sizeof(ModelPushConstant), .stageFlags = EShaderStage::Vertex}}, {_frameDSL});

    _pipelineCI = GraphicsPipelineCreateInfo{
        .pipelineRenderingInfo = {
            .label                 = "Shadow Map Generate",
            .depthAttachmentFormat = EFormat::D32_SFLOAT,
        },
        .pipelineLayout = _pipelineLayout.get(),
        .shaderDesc     = ShaderDesc{
            .shaderName        = "Shadow/CombinedShadowMappingGenerate.glsl",
            .vertexBufferDescs = {VertexBufferDescription{.slot = 0, .pitch = sizeof(ya::Vertex)}},
            .vertexAttributes  = {{.bufferSlot = 0, .location = 0, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, position)}},
        },
        .dynamicFeatures    = {EPipelineDynamicFeature::Viewport, EPipelineDynamicFeature::Scissor},
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = {.polygonMode = EPolygonMode::Fill, .cullMode = ECullMode::Front, .frontFace = EFrontFaceType::CounterClockWise},
        .depthStencilState  = {.bDepthTestEnable = true, .bDepthWriteEnable = true, .depthCompareOp = ECompareOp::LessOrEqual},
        .colorBlendState    = {.attachments = {}},
        .viewportState      = {.viewports = {Viewport::defaults()}, .scissors = {Scissor::defaults()}},
    };
    refreshPipelineFromRenderTarget();
    _pipeline = IGraphicsPipeline::create(_render);
    YA_CORE_ASSERT(_pipeline && _pipeline->recreate(_pipelineCI), "Failed to create ShadowStage pipeline");

    _dsp = IDescriptorPool::create(_render, DescriptorPoolCreateInfo{
        .label     = "ShadowStage_DSP",
        .maxSets   = MAX_FLIGHTS_IN_FLIGHT,
        .poolSizes = {{.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = MAX_FLIGHTS_IN_FLIGHT}},
    });

    FrameUBO initialData{};
    for (uint32_t i = 0; i < MAX_FLIGHTS_IN_FLIGHT; ++i) {
        _frameUBO[i] = IBuffer::create(
            _render,
            BufferCreateInfo{
                .label       = std::format("ShadowStage_Frame_UBO_{}", i),
                .usage       = EBufferUsage::UniformBuffer,
                .size        = sizeof(FrameUBO),
                .memoryUsage = EMemoryUsage::CpuToGpu,
            });
        _frameUBO[i]->writeData(&initialData, sizeof(FrameUBO), 0);

        _frameDS[i] = _dsp->allocateDescriptorSets(_frameDSL);
        _render->getDescriptorHelper()->updateDescriptorSets({
            IDescriptorSetHelper::writeOneUniformBuffer(_frameDS[i], 0, _frameUBO[i].get()),
        });
    }
}

void ShadowStage::destroy()
{
    for (auto& ubo : _frameUBO) ubo.reset();
    _dsp.reset();
    _frameDSL.reset();
    _pipelineLayout.reset();
    _pipeline.reset();
    _shadowMapRT.reset();
    _render = nullptr;
}

void ShadowStage::prepare(const RenderStageContext& ctx)
{
    if (_pipeline) {
        _pipeline->beginFrame();
    }

    if (!ctx.frameData) return;
    const auto& fd = *ctx.frameData;
    const uint32_t fi = ctx.flightIndex;

    const uint32_t pointLightCount = _bEnablePointLightShadow ? std::min(fd.numPointLights, _maxPointLightShadowCount) : 0;

    FrameUBO frameData{
        .directionalLightMatrix = fd.directionalLight.viewProjection,
        .numPointLights         = pointLightCount,
        .hasDirectionalLight    = fd.bHasDirectionalLight ? 1u : 0u,
    };
    _lastPreparedPointLightCount = pointLightCount;

    for (uint32_t i = 0; i < pointLightCount; ++i) {
        const auto& pl = fd.pointLights[i];
        const glm::vec3& pos = pl.position;
        const glm::mat4 faceProj = FMath::perspective(glm::radians(90.0f), 1.0f, pl.nearPlane, pl.farPlane);

        for (int face = ECubeFace::CubeFace_PosX; face < ECubeFace::CubeFace_Count; ++face) {
            glm::mat4 view{};
            const glm::vec3 down = glm::vec3{0, -1, 0};
            const glm::vec3 backward = {0, 0, 1};

            if constexpr (FMath::Vector::IsRightHanded) {
                switch ((ECubeFace)face) {
                case CubeFace_PosX: {
                    view = FMath::lookAt(pos, pos + glm::vec3(1, 0, 0), down);
                    break;
                }
                case CubeFace_NegX: {
                    view = FMath::lookAt(pos, pos + glm::vec3(-1, 0, 0), down);
                    break;
                }
                case CubeFace_PosY: {
                    view = FMath::lookAt(pos, pos + glm::vec3(0, 1, 0), backward);
                    break;
                }
                case CubeFace_NegY: {
                    view = FMath::lookAt(pos, pos + glm::vec3(0, -1, 0), -backward);
                    break;
                }
                case CubeFace_PosZ: {
                    view = FMath::lookAt(pos, pos + glm::vec3(0, 0, 1), down);
                    break;
                }
                case CubeFace_NegZ: {
                    view = FMath::lookAt(pos, pos + glm::vec3(0, 0, -1), down);
                    break;
                }
                case CubeFace_Count: {
                    UNREACHABLE();
                }
                }
            }

            frameData.pointLights[i].matrix[face] = faceProj * view;
            frameData.pointLights[i].pos          = pos;
            frameData.pointLights[i].farPlane     = pl.farPlane;
        }
    }

    _frameUBO[fi]->writeData(&frameData, sizeof(FrameUBO), 0);
}

void ShadowStage::execute(const RenderStageContext& ctx)
{
    if (!ctx.cmdBuf || !ctx.frameData) return;

    auto* cmdBuf = ctx.cmdBuf;
    const uint32_t fi = ctx.flightIndex;

    cmdBuf->debugBeginLabel("ShadowStage");
    cmdBuf->bindPipeline(_pipeline.get());

    if (_bAutoBindViewportScissor) {
        cmdBuf->setViewport(0.0f, 0.0f, static_cast<float>(_shadowExtent.width), static_cast<float>(_shadowExtent.height), 0.0f, 1.0f);
        cmdBuf->setScissor(0, 0, _shadowExtent.width, _shadowExtent.height);
    }

    cmdBuf->bindDescriptorSets(_pipelineLayout.get(), 0, {_frameDS[fi]});

    auto drawItems = [&](const std::vector<RenderDrawItem>& items)
    {
        for (const auto& item : items) {
            if (!item.mesh) {
                continue;
            }
            ModelPushConstant pc{.model = item.worldMatrix};
            cmdBuf->pushConstants(_pipelineLayout.get(), EShaderStage::Vertex, 0, sizeof(ModelPushConstant), &pc);
            item.mesh->draw(cmdBuf);
        }
    };

    const auto& fd = *ctx.frameData;
    drawItems(fd.pbrDrawItems);
    drawItems(fd.phongDrawItems);
    drawItems(fd.unlitDrawItems);
    drawItems(fd.simpleDrawItems);
    drawItems(fd.fallbackDrawItems);

    cmdBuf->debugEndLabel();
}

void ShadowStage::renderGUI()
{
    if (!ImGui::TreeNode("ShadowStage")) {
        return;
    }
    ImGui::Text("RT: %ux%u", _shadowExtent.width, _shadowExtent.height);
    ImGui::Text("Prepared point lights: %u", _lastPreparedPointLightCount);
    ImGui::Text("Point shadow budget: %u", _maxPointLightShadowCount);
    ImGui::Checkbox("Auto Viewport/Scissor", &_bAutoBindViewportScissor);
    ImGui::DragFloat("Receiver Depth Bias", &_bias, 0.0001f, 0.0f, 0.1f, "%.5f");
    ImGui::DragFloat("Receiver Normal Bias", &_normalBias, 0.0001f, 0.0f, 0.1f, "%.5f");
    _pipeline->renderGUI();
    ImGui::TreePop();
}

} // namespace ya