#pragma once
#include "BaseMaterialSystem.h"


#include "Core/App/App.h"
#include "Platform/Render/Vulkan/VulkanRender.h"

#include "ECS/Component.h"
#include "ECS/Component/CameraComponent.h"

#include "Math/Geometry.h"
#include "Render/Core/RenderTarget.h"

#include "Render/Mesh.h"

#include "vulkan/vulkan.h"

#include "ECS/Component/Material/BaseMaterialComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/Entity.h"



#include "imgui.h"

namespace ya
{


void BaseMaterialSystem::onInit(VulkanRenderPass *renderPass)
{
    _label                 = "BaseMaterialSystem";
    VulkanRender *vkRender = getVulkanRender();

    auto _sampleCount = ESampleCount::Sample_1;

    constexpr auto size = sizeof(BaseMaterialSystem::PushConstant);
    YA_CORE_DEBUG("BaseMaterialSystem PushConstant size: {}", size);
    PipelineLayout pipelineLayout{
        .label         = "BaseMaterialSystem_PipelineLayout",
        .pushConstants = {
            PushConstantRange{
                .offset     = 0,
                .size       = size,
                .stageFlags = EShaderStage::Vertex,
            },
        },
        .descriptorSetLayouts = {},
    };

    _pipelineLayout = std::make_shared<VulkanPipelineLayout>(vkRender, pipelineLayout.label);
    // _pipelineLayout->create(pipelineLayout.pushConstants, { pipelineLayout.descriptorSetLayouts[0], });
    _pipelineLayout->create(pipelineLayout.pushConstants, {});


    GraphicsPipelineCreateInfo pipelineCI{
        .subPassRef = 0,
        // .pipelineLayout   = pipelineLayout,
        .shaderCreateInfo = ShaderCreateInfo{
            .shaderName        = "Test/BaseMaterial.glsl",
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
        .dynamicFeatures = EPipelineDynamicFeature::Scissor | // the imgui required this feature as I did not set the dynamical render feature
                           EPipelineDynamicFeature::Viewport,
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = RasterizationState{
            .polygonMode = EPolygonMode::Fill,
            .frontFace   = EFrontFaceType::CounterClockWise, // GL
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
                    .colorWriteMask      = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A,
                },
            },
        },
        .viewportState = ViewportState{
            .viewports = {
                {
                    .x        = 0,
                    .y        = 0,
                    .width    = static_cast<float>(vkRender->getSwapChain()->getWidth()),
                    .height   = static_cast<float>(vkRender->getSwapChain()->getHeight()),
                    .minDepth = 0.0f,
                    .maxDepth = 1.0f,
                },
            },
            .scissors = {Scissor{
                .offsetX = 0,
                .offsetY = 0,
                .width   = static_cast<uint32_t>(vkRender->getSwapChain()->getWidth()),
                .height  = static_cast<uint32_t>(vkRender->getSwapChain()->getHeight()),
            }},
        },
    };
    _pipeline = std::make_shared<VulkanPipeline>(vkRender, renderPass, _pipelineLayout.get());
    _pipeline->recreate(pipelineCI);
}

void BaseMaterialSystem::onDestroy()
{
    _pipeline.reset();
    _pipelineLayout.reset();
}

void BaseMaterialSystem::onUpdate(float deltaTime)
{
}

void BaseMaterialSystem::onRender(void *cmdBuf, RenderTarget *rt)
{

    auto vkRender = getVulkanRender();
    auto vkCmdBuf = (VkCommandBuffer)cmdBuf;
    auto scene    = getScene();
    if (!scene) {
        return;
    }
    const auto &view = scene->getRegistry().view<TransformComponent, BaseMaterialComponent>();
    if (view.begin() == view.end()) {
        return;
    }

    _pipeline->bind(vkCmdBuf);
    auto curFrameBuffer = rt->getFrameBuffer();

#pragma region Dynamic State
    // need set VkPipelineDynamicStateCreateInfo
    // or those properties should be modified in the pipeline recreation if needed.
    // but sometimes that we want to enable depth or color-blend state dynamically
    VkViewport viewport{
        .x        = 0,
        .y        = 0,
        .width    = static_cast<float>(curFrameBuffer->getWidth()),
        .height   = static_cast<float>(curFrameBuffer->getHeight()),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(vkCmdBuf, 0, 1, &viewport);

    VkRect2D scissor{
        .offset = {0, 0},
        .extent = vkRender->getSwapChain()->getExtent(),
    };
    vkCmdSetScissor(vkCmdBuf, 0, 1, &scissor);
#pragma endregion

    // FIXME: this is just a test material, we should move the projection and view to frame data UBO, not push constant

    rt->getViewAndProjMatrix(pc.view, pc.projection);

    for (const auto entity : view) {
        const auto &[tc, bmc] = view.get(entity);
        // TODO: culling works

        const auto &material2MeshIds = bmc.getMaterial2MeshIDs();
        for (const auto &[material, meshIds] : material2MeshIds) {
            if (!material) {
                continue;
            }

            pc.model     = tc.getTransform();
            pc.colorType = material->colorType;

            // pc.colorType = sin(TimeManager::get()->now().time_since_epoch().count() * 0.001);

            vkCmdPushConstants(vkCmdBuf,
                               _pipelineLayout->getHandle(),
                               VK_SHADER_STAGE_VERTEX_BIT,
                               0,
                               sizeof(PushConstant),
                               &pc);

            for (const auto &idx : meshIds) {
                auto mesh = bmc.getMesh(idx);
                if (!mesh || !mesh->getVertexBuffer() || !mesh->getIndexBuffer()) {
                    continue;
                }
                // ++totalCount;

                VkBuffer vertexBuffers[] = {mesh->getVertexBuffer()->getHandle()};
                // current no need to support subbuffer
                VkDeviceSize offsets[] = {0};
                vkCmdBindVertexBuffers(vkCmdBuf, 0, 1, vertexBuffers, offsets);
                vkCmdBindIndexBuffer(vkCmdBuf,
                                     mesh->getIndexBuffer()->getHandle(),
                                     0,
                                     VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(vkCmdBuf,
                                 mesh->getIndexCount(), // index count
                                 1,                     // instance count: 9 cubes
                                 0,                     // first index
                                 0,                     // vertex offset, this for merge vertex buffer?
                                 0);
            }
        }
    }
}

} // namespace ya