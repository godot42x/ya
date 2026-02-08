#pragma once
#include "SimpleMaterialSystem.h"


#include "Core/App/App.h"

#include "ECS/Component/MeshComponent.h"

#include "Core/Math/Geometry.h"
#include "Render/Core/IRenderTarget.h"

#include "Render/Core/Swapchain.h"
#include "Render/Mesh.h"



#include "ECS/Component/DirectionComponent.h"
#include "ECS/Component/Material/SimpleMaterialComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/Entity.h"

#include "Resource/PrimitiveMeshCache.h"

#include "Core/Math/Math.h"



#include "imgui.h"

namespace ya
{



void SimpleMaterialSystem::onInit(IRenderPass *renderPass, const PipelineRenderingInfo & pipelineRenderingInfo)
{
    _label       = "SimpleMaterialSystem";
    auto *render = getRender();

    auto _sampleCount = ESampleCount::Sample_1;

    constexpr auto size = sizeof(SimpleMaterialSystem::PushConstant);
    YA_CORE_DEBUG("SimpleMaterialSystem PushConstant size: {}", size);
    PipelineLayoutDesc pipDesc{
        .label         = "SimpleMaterialSystem_PipelineLayout",
        .pushConstants = {
            PushConstantRange{
                .offset     = 0,
                .size       = size,
                .stageFlags = EShaderStage::Vertex,
            },
        },
        .descriptorSetLayouts = {},
    };

    // Use factory method to create pipeline layout
    _pipelineLayout = IPipelineLayout::create(render, pipDesc.label, pipDesc.pushConstants, {});


    GraphicsPipelineCreateInfo pipelineCI{
        .subPassRef            = 0,
        .renderPass            = renderPass,
        .pipelineRenderingInfo = pipelineRenderingInfo,
        .pipelineLayout        = _pipelineLayout.get(),

        .shaderDesc = ShaderDesc{
            .shaderName        = "Test/SimpleMaterial.glsl",
            .bDeriveFromShader = false,
            .vertexBufferDescs = {
                VertexBufferDescription{
                    .slot  = 0,
                    .pitch = sizeof(ya::Vertex),
                },
            },
            .vertexAttributes = {
                // (location=0) in vec3 aPos,
                VertexAttribute{
                    .bufferSlot = 0,
                    .location   = 0,
                    .format     = EVertexAttributeFormat::Float3,
                    .offset     = offsetof(ya::Vertex, position),
                },
                //  texcoord
                VertexAttribute{
                    .bufferSlot = 0, // same buffer slot
                    .location   = 1,
                    .format     = EVertexAttributeFormat::Float2,
                    .offset     = offsetof(ya::Vertex, texCoord0),
                },
                // normal
                VertexAttribute{
                    .bufferSlot = 0, // same buffer slot
                    .location   = 2,
                    .format     = EVertexAttributeFormat::Float3,
                    .offset     = offsetof(ya::Vertex, normal),
                },
            },
        },

        // define what state need to dynamically modified in render pass execution
        .dynamicFeatures    = {EPipelineDynamicFeature::Scissor, // the imgui required this feature as I did not set the dynamical render feature
                               EPipelineDynamicFeature::Viewport},
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = RasterizationState{
            .polygonMode = EPolygonMode::Fill,
            .frontFace   = EFrontFaceType::CounterClockWise, // GL
            // .frontFace = EFrontFaceType::ClockWise, // VK
        },
        .multisampleState = MultisampleState{
            .sampleCount          = _sampleCount,
            .bSampleShadingEnable = false,
        },
        .depthStencilState = DepthStencilState{
            .bDepthTestEnable       = true,
            .bDepthWriteEnable      = true,
            .depthCompareOp         = ECompareOp::Less,
            .bDepthBoundsTestEnable = false,
            .bStencilTestEnable     = false,
            .minDepthBounds         = 0.0f,
            .maxDepthBounds         = 1.0f,
        },
        .colorBlendState = ColorBlendState{
            .attachments = {
                ColorBlendAttachmentState{
                    .index               = 0,
                    .bBlendEnable        = false,
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
        .viewportState = ViewportState{
            .viewports = {
                {
                    .x        = 0,
                    .y        = 0,
                    .width    = static_cast<float>(render->getSwapchain()->getExtent().width),
                    .height   = static_cast<float>(render->getSwapchain()->getExtent().height),
                    .minDepth = 0.0f,
                    .maxDepth = 1.0f,
                },
            },
            .scissors = {Scissor{
                .offsetX = 0,
                .offsetY = 0,
                .width   = render->getSwapchain()->getExtent().width,
                .height  = render->getSwapchain()->getExtent().height,
            }},
        },
    };
    // Use factory method to create graphics pipeline
    _pipeline = IGraphicsPipeline::create(render);
    _pipeline->recreate(pipelineCI);
}


void SimpleMaterialSystem::onDestroy()
{
    _pipeline.reset();
    _pipelineLayout.reset();
}

void SimpleMaterialSystem::onRenderGUI()
{
    IMaterialSystem::onRenderGUI();
    ImGui::Combo("Default Color Type", &_defaultColorType, "Normal\0UV\0Fixed");
}


void SimpleMaterialSystem::onRender(ICommandBuffer *cmdBuf, FrameContext *ctx)
{

    auto render = getRender();
    auto scene  = getActiveScene();
    if (!scene) {
        return;
    }
    const auto &view1 = scene->getRegistry().view<TransformComponent, SimpleMaterialComponent, MeshComponent>();
    const auto &view2 = scene->getRegistry().view<TransformComponent, DirectionComponent>();
    if (view1.begin() == view1.end() && view2.begin() == view2.end()) {
        return;
    }

    // Wrap void* in VulkanCommandBuffer for generic bind call
    // Get viewport extent from App
    auto fbExtent = ctx->extent;
    cmdBuf->bindPipeline(_pipeline.get());

#pragma region Dynamic State
    // need set VkPipelineDynamicStateCreateInfo
    // or those properties should be modified in the pipeline recreation if needed.
    // but sometimes that we want to enable depth or color-blend state dynamically
    VkViewport viewport{
        .x        = 0,
        .y        = 0,
        .width    = static_cast<float>(fbExtent.width),
        .height   = static_cast<float>(fbExtent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    if (bReverseViewportY) {
        viewport.y      = static_cast<float>(fbExtent.height);
        viewport.height = -static_cast<float>(fbExtent.height);
    }
    cmdBuf->setViewport(viewport.x, viewport.y, viewport.width, viewport.height, viewport.minDepth, viewport.maxDepth);

    auto     extent = render->getSwapchain()->getExtent();
    VkRect2D scissor{
        .offset = {0, 0},
        .extent = {extent.width, extent.height},
    };
    // TODO: scissor count and nth scissor?
    cmdBuf->setScissor(scissor.offset.x, scissor.offset.y, scissor.extent.width, scissor.extent.height);
    // vkCmdSetScissor(vkCmdBuf, 0, 1, &scissor);
#pragma endregion

    // Use passed camera context
    pc.view       = ctx->view;
    pc.projection = ctx->projection;

    // for (const auto entity : view1) {
    view1.each([this, &cmdBuf](auto &tc, auto &smc, auto &mc) {
        // const auto &[tc, smc, meshComp] = view1.get(entity);
        // TODO: culling works

        SimpleMaterial *material = smc.getMaterial();
        if (!material) {
            return;
            // continue;
        }

        pc.model     = tc.getTransform();
        pc.colorType = material->colorType;

        // pc.colorType = sin(TimeManager::get()->now().time_since_epoch().count() * 0.001);

        cmdBuf->pushConstants(_pipelineLayout.get(),
                              EShaderStage::Vertex,
                              0,
                              sizeof(PushConstant),
                              &pc);

        mc.getMesh()->draw(cmdBuf);
    });
    auto cone     = PrimitiveMeshCache::get().getMesh(EPrimitiveGeometry::Cone);
    auto cylinder = PrimitiveMeshCache::get().getMesh(EPrimitiveGeometry::Cylinder);

    glm::mat4 coneLocalTransf =
        glm::rotate(glm::mat4(1.0), glm::radians(90.0f), glm::vec3(1, 0, 0)) *
        glm::scale(glm::mat4(1.0), glm::vec3(0.3f, 1.0f, 0.3f));
    glm::mat4 cylinderLocalTransf =
        glm::rotate(glm::mat4(1.0), glm::radians(90.0f), glm::vec3(1, 0, 0)) *
        glm::scale(glm::mat4(1.0), glm::vec3(0.1f, 1.0f, 0.1f));

    pc.colorType = _defaultColorType;
    for (auto entity : view2) {
        const auto &[tc, dc] = view2.get(entity);

        glm::mat4 worldTransform = glm::translate(glm::mat4(1.0), tc.getWorldPosition()) *
                                   glm::mat4_cast(glm::quat(glm::radians(tc.getRotation())));

        // Draw cone (arrow tip pointing forward)
        pc.model = glm::translate(glm::mat4(1.0), -tc.getForward()) *
                   coneLocalTransf *
                   worldTransform;
        cmdBuf->pushConstants(_pipelineLayout.get(),
                              EShaderStage::Vertex,
                              0,
                              sizeof(PushConstant),
                              &pc);
        cone->draw(cmdBuf);

        // Draw cylinder (arrow shaft)
        pc.model = worldTransform * cylinderLocalTransf;
        cmdBuf->pushConstants(_pipelineLayout.get(),
                              EShaderStage::Vertex,
                              0,
                              sizeof(PushConstant),
                              &pc);
        cylinder->draw(cmdBuf);
    }
}

} // namespace ya