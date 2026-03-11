#include "ShadowMapping.h"
#include "ECS/Component/MeshComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "Render/Core/Swapchain.h"
#include "Render/Render.h"
#include "Scene/Scene.h"
#include "imgui.h"

namespace ya
{

void ShadowMapping::onInitImpl(const InitParams& initParams)
{
    auto render       = getRender();
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

            .shaderName = "Shadow/CombinedShadowMappingGenerate.glsl",
            // .shaderName        = "CombineShadowMappingGenerate.slang",
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
            .defines = {
                std::format("MAX_POINT_LIGHTS {}", MAX_POINT_LIGHTS),
            }},
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

    FrameUBO initialFrameData{};

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
    if (!ctx) {
        return;
    }
    if (ctx->extent.width <= 0 || ctx->extent.height <= 0) {
        return;
    }
    auto* scene = getActiveScene();
    if (!scene) {
        return;
    }


    // TODO: make the frameCtx as one descriptor set to avoid copy: ctx -> frameData -> frameUBO -> GPU
    FrameUBO frameData{
        .directionalLightMatrix = ctx->directionalLight.viewProjection,
        .numPointLights         = ctx->numPointLights,
        .hasDirectionalLight    = ctx->bHasDirectionalLight,
    };
    for (uint32_t i = 0; i < ctx->numPointLights; ++i) {
        frameData.pointLightMatrices[i] = ctx->pointLights[i].viewProjection;
    }
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
    ImGui::Checkbox("Auto Viewport/Scissor", &_bAutoBindViewportScissor);
    ImGui::DragFloat("Receiver Depth Bias", &_bias, 0.0001f, 0.0f, 0.1f, "%.5f");
    ImGui::DragFloat("Receiver Normal Bias", &_normalBias, 0.0001f, 0.0f, 0.1f, "%.5f");
}

} // namespace ya
