#include "ShadowMapping.h"
#include "Core/Math/Math.h"
#include "ECS/Component/DirectionalLightComponent.h"
#include "ECS/Component/MeshComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "Render/Core/Swapchain.h"
#include "Render/Render.h"
#include "Scene/Scene.h"
#include "glm/gtc/matrix_transform.hpp"
#include "imgui.h"

namespace ya
{

void ShadowMapping::onInitImpl(const InitParams& initParams)
{
    auto render = getRender();
    bReverseViewportY = false;

    auto DSLs    = IDescriptorSetLayout::create(render, _pipelineLayoutDesc.descriptorSetLayouts);
    _dslPerFrame = DSLs[0];

    _pipelineLayout = IPipelineLayout::create(render,
                                              _pipelineLayoutDesc.label,
                                              _pipelineLayoutDesc.pushConstants,
                                              DSLs);

    GraphicsPipelineCreateInfo ci = {
        .renderPass            = initParams.renderPass,
        .pipelineRenderingInfo = initParams.pipelineRenderingInfo,
        .pipelineLayout        = _pipelineLayout.get(),
        .shaderDesc            = {
                       .shaderName        = "SimpleDepthShader.glsl",
                       .bDeriveFromShader = false,
                       .vertexBufferDescs = {
                VertexBufferDescription{
                               .slot  = 0,
                               .pitch = sizeof(ya::Vertex),
                },
            },
                       .vertexAttributes = {
                VertexAttribute{
                               .bufferSlot = 0,
                               .location   = 0,
                               .format     = EVertexAttributeFormat::Float3,
                               .offset     = offsetof(ya::Vertex, position),
                },
            },
        },
        .dynamicFeatures = {
            EPipelineDynamicFeature::Viewport,
            EPipelineDynamicFeature::Scissor,
        },
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = RasterizationState{
            .polygonMode = EPolygonMode::Fill,
            .cullMode    = ECullMode::Back,
            .frontFace   = EFrontFaceType::CounterClockWise,
        },
        .depthStencilState = DepthStencilState{
            .bDepthTestEnable       = true,
            .bDepthWriteEnable      = true,
            .depthCompareOp         = ECompareOp::LessOrEqual,
            .bDepthBoundsTestEnable = false,
            .bStencilTestEnable     = false,
        },
        .colorBlendState = ColorBlendState{
            .attachments = {},
        },
        .viewportState = ViewportState{
            .viewports = {Viewport::defaults()},
            .scissors  = {Scissor::defaults()},
        },
    };
    _pipeline = IGraphicsPipeline::create(render);
    bool ok   = _pipeline->recreate(ci);
    YA_CORE_ASSERT(ok, "Failed to create ShadowMapping graphics pipeline");

    _DSP = IDescriptorPool::create(
        render,
        DescriptorPoolCreateInfo{
            .maxSets   = SHADOW_PER_FRAME_SET,
            .poolSizes = {
                DescriptorPoolSize{
                    .type            = EPipelineDescriptorType::UniformBuffer,
                    .descriptorCount = SHADOW_PER_FRAME_SET,
                },
            },
        });

    std::vector<DescriptorSetHandle> frameSets;
    _DSP->allocateDescriptorSets(_dslPerFrame, SHADOW_PER_FRAME_SET, frameSets);

    std::vector<WriteDescriptorSet> writes;
    writes.resize(SHADOW_PER_FRAME_SET);

    FrameUBO initialFrameData{
        .lightMatrix = glm::mat4(1.0f),
    };

    for (uint32_t i = 0; i < SHADOW_PER_FRAME_SET; ++i) {
        _dsPerFrame[i] = frameSets[i];
        _frameUBO[i]   = IBuffer::create(
            render,
            BufferCreateInfo{
                  .label         = std::format("Shadow_Frame_UBO_{}", i),
                  .usage         = EBufferUsage::UniformBuffer,
                  .size          = sizeof(FrameUBO),
                  .memProperties = EMemoryProperty::HostVisible | EMemoryProperty::HostCoherent,
            });
        _frameUBO[i]->writeData(&initialFrameData, sizeof(FrameUBO), 0);

        writes[i] = IDescriptorSetHelper::genSingleBufferWrite(
            _dsPerFrame[i],
            0,
            EPipelineDescriptorType::UniformBuffer,
            _frameUBO[i].get());
    }
    render->getDescriptorHelper()->updateDescriptorSets(writes, {});
}

void ShadowMapping::onRender(ICommandBuffer* cmdBuf, const FrameContext* ctx)
{
    if (!_shadowMapRT || !ctx || !cmdBuf) {
        return;
    }

    _shadowExtent = _shadowMapRT->getExtent();
    if (_shadowExtent.width == 0 || _shadowExtent.height == 0) {
        return;
    }

    updateLightFromScene();

    FrameUBO frameData{
        .lightMatrix = _uLightCameraData.viewProjection,
    };
    _frameUBO[_index]->writeData(&frameData, sizeof(FrameUBO), 0);

    cmdBuf->bindPipeline(_pipeline.get());

    float viewportY      = 0.0f;
    float viewportHeight = static_cast<float>(_shadowExtent.height);
    if (bReverseViewportY) {
        viewportY      = static_cast<float>(_shadowExtent.height);
        viewportHeight = -static_cast<float>(_shadowExtent.height);
    }

    if (_bAutoBindViewportScissor) {
        cmdBuf->setViewport(0.0f, viewportY, static_cast<float>(_shadowExtent.width), viewportHeight, 0.0f, 1.0f);
        cmdBuf->setScissor(0, 0, _shadowExtent.width, _shadowExtent.height);
    }

    cmdBuf->bindDescriptorSets(_pipelineLayout.get(),
                               0,
                               {
                                   _dsPerFrame[_index],
                               });

    auto* scene = getActiveScene();
    if (!scene) {
        advance();
        return;
    }

    auto view = scene->getRegistry().view<MeshComponent, TransformComponent>();
    for (const auto& [entity, mc, tc] : view.each()) {
        auto* mesh = mc.getMesh();
        if (!mesh) {
            continue;
        }

        ModelPushConstant pushConst{
            .model = tc.getTransform(),
        };
        cmdBuf->pushConstants(_pipelineLayout.get(),
                              _pipelineLayoutDesc.pushConstants[0].stageFlags,
                              0,
                              sizeof(ModelPushConstant),
                              &pushConst);

        mesh->draw(cmdBuf);
    }

    advance();
}

void ShadowMapping::onDestroy()
{
    for (auto& ubo : _frameUBO) {
        ubo.reset();
    }
    _DSP.reset();
    _dslPerFrame.reset();
    _pipelineLayout.reset();
    _pipeline.reset();
    _shadowMapRT.reset();
}

void ShadowMapping::onRenderGUI()
{
    Super::onRenderGUI();
    ImGui::Text("ShadowMapping Scaffold");
    ImGui::Text("RT: %ux%u", _shadowExtent.width, _shadowExtent.height);
    ImGui::Text("Light Dir: %.3f %.3f %.3f", _uLightCameraData.direction.x, _uLightCameraData.direction.y, _uLightCameraData.direction.z);
    ImGui::Checkbox("Auto Viewport/Scissor", &_bAutoBindViewportScissor);
    ImGui::DragFloat("Depth Bias", &_bias, 0.0001f, 0.0f, 0.1f, "%.5f");
    ImGui::DragFloat("Normal Bias", &_normalBias, 0.0001f, 0.0f, 0.1f, "%.5f");
    ImGui::DragFloat("Light Distance", &_lightDistance, 0.1f, 1.0f, 200.0f);
    ImGui::DragFloat("Ortho HalfWidth", &_orthoHalfWidth, 0.1f, 0.1f, 200.0f);
    ImGui::DragFloat("Ortho HalfHeight", &_orthoHalfHeight, 0.1f, 0.1f, 200.0f);
    ImGui::DragFloat("Near Plane", &_nearPlane, 0.01f, 0.001f, 50.0f);
    ImGui::DragFloat("Far Plane", &_farPlane, 0.1f, 1.0f, 500.0f);
}

void ShadowMapping::updateLightFromScene()
{
    auto* scene = getActiveScene();
    if (!scene) {
        return;
    }

    glm::vec3 lightDir = _uLightCameraData.direction;
    bool      bFound   = false;

    for (const auto& [entity, dlc, tc] : scene->getRegistry().view<DirectionalLightComponent, TransformComponent>().each()) {
        lightDir = glm::normalize(tc.getForward());
        bFound   = true;
        break;
    }

    if (!bFound) {
        for (const auto& [entity, dlc] : scene->getRegistry().view<DirectionalLightComponent>().each()) {
            lightDir = glm::normalize(dlc._direction);
            bFound   = true;
            break;
        }
    }

    if (!bFound) {
        return;
    }

    _uLightCameraData.direction = lightDir;
    // if (bReverseViewportY) {
    //     _uLightCameraData.projection = FMath::orthographic(-_orthoHalfWidth,
    //                                                        _orthoHalfWidth,
    //                                                        _orthoHalfHeight,
    //                                                        -_orthoHalfHeight,
    //                                                        _nearPlane,
    //                                                        _farPlane);
    // }
    // else {
    _uLightCameraData.projection = FMath::orthographic(-_orthoHalfWidth,
                                                       _orthoHalfWidth,
                                                       -_orthoHalfHeight,
                                                       _orthoHalfHeight,
                                                       _nearPlane,
                                                       _farPlane);
    // }
    _uLightCameraData.view           = glm::lookAt(-lightDir * _lightDistance, glm::vec3(0.0f), glm::vec3(0, 1, 0));
    _uLightCameraData.viewProjection = _uLightCameraData.projection * _uLightCameraData.view;
}

} // namespace ya
