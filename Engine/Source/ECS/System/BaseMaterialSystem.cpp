#pragma once
#include "BaseMaterialSystem.h"


#include "Core/App/App.h"

#include "ECS/Component.h"
#include "ECS/Component/CameraComponent.h"

#include "Math/Geometry.h"
#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Render/Core/RenderTarget.h"

#include "Render/Mesh.h"
#include "vulkan/vulkan.h"

#include "ECS/Component/Material/BaseMaterialComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/Entity.h"
#include "ECS/System.h"

#include "Core/TimeManager.h"


#include "imgui.h"

namespace ya
{


void BaseMaterialSystem::onInit(VulkanRenderPass *renderPass)
{
    auto vkRender = App::get()->getRender<VulkanRender>();

    auto _sampleCount = ESampleCount::Sample_1;

    PipelineLayout pipelineLayout{
        .pushConstants = {
            PushConstantRange{
                .offset     = 0,
                .size       = sizeof(PushConstant),
                .stageFlags = EShaderStage::Vertex,
            },
        },
        .descriptorSetLayouts = {
            // DescriptorSetLayout{
            //     .set      = 0,
            //     .bindings = {
            //         // uGBuffer
            //         DescriptorSetLayoutBinding{
            //             .binding         = 0,
            //             .descriptorType  = EPipelineDescriptorType::UniformBuffer,
            //             .descriptorCount = 1,
            //             .stageFlags      = EShaderStage::Vertex,
            //         },
            //         // uInstanceBuffer
            //         DescriptorSetLayoutBinding{
            //             .binding         = 1,
            //             .descriptorType  = EPipelineDescriptorType::UniformBuffer,
            //             .descriptorCount = 1,
            //             .stageFlags      = EShaderStage::Vertex,
            //         },
            //         // uTexture0
            //         DescriptorSetLayoutBinding{
            //             .binding         = 2,
            //             .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
            //             .descriptorCount = 1,
            //             .stageFlags      = EShaderStage::Fragment,
            //         },
            //         // uTexture1
            //         DescriptorSetLayoutBinding{
            //             .binding         = 3,
            //             .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
            //             .descriptorCount = 1,
            //             .stageFlags      = EShaderStage::Fragment,
            //         },
            //         // uTextures
            //         DescriptorSetLayoutBinding{
            //             .binding         = 4,
            //             .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
            //             .descriptorCount = 16,
            //             .stageFlags      = EShaderStage::Fragment,
            //         },
            // },
            // },
        },
    };

    _pipelineLayout = std::make_shared<VulkanPipelineLayout>(vkRender);
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


    vkRender->setDebugObjectName(VK_OBJECT_TYPE_PIPELINE_LAYOUT, _pipelineLayout->getHandle(), "BaseMaterialSystem_PipelineLayout");
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
    // culledCount = 0;
    // totalCount  = 0;

    auto vkRender = App::get()->getRender<VulkanRender>();
    auto vkCmdBuf = (VkCommandBuffer)cmdBuf;
    auto scene    = App::get()->getScene();
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


    // Set scissor (required by imgui , and cause I must call this here)
    VkRect2D scissor{
        .offset = {0, 0},
        .extent = vkRender->getSwapChain()->getExtent(),
    };
    vkCmdSetScissor(vkCmdBuf, 0, 1, &scissor);
#pragma endregion

    // glm::vec3 camPos;
    // glm::vec3 camRot;
    // float     fov;
    // HACK: remove this
    if (auto cam = rt->getCamera(); cam && bEntityCamera) {
        if (cam->hasComponent<CameraComponent>())
        {
            auto &cc          = cam->getComponent<CameraComponent>();
            pc.viewProjection = cc.getProjection() * cc.getView();
            auto &tc          = cam->getComponent<TransformComponent>();
            // camPos            = tc.getPosition();
            // camRot            = tc._rotation;
            // fov               = cc._fov;
        }
    }
    else {
        pc.viewProjection = App::get()->camera.getViewProjectionMatrix();
        // camPos            = App::get()->camera.getPosition();
        // camRot            = App::get()->camera._rotation;
    }


    for (const auto entity : view) {
        // const auto &[tc, mc, bmc] = view.get<TransformComponent, MeshComponent, BaseMaterialComponent>(entity);
        const auto &[tc, bmc] = view.get(entity);
        // auto toEntity = tc.getPosition() - camPos;
        // if (glm::length(toEntity) > maxViewDistance) {
        //     // too far
        //     ++culledCount;
        //     continue;
        // }
        // float proj = glm::dot(toEntity, camRot);
        // if (proj > 0.001f) {
        //     // backface culling
        //     ++culledCount;
        //     continue;
        // }

        const auto &material2MeshIds = bmc.getMaterial2MeshIds();
        for (const auto &[material, meshIds] : material2MeshIds) {
            if (!material && material->_index == entt::type_hash<BaseMaterialComponent>::value()) {
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

void BaseMaterialSystem::onRenderGUI()
{
    ImGui::CollapsingHeader("BaseMaterialSystem Settings", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::DragFloat("Max View Distance", &maxViewDistance, 0.1f, 1.f, 1000.0f);
    // ImGui::Text("Culled Count: %d/%d", culledCount, totalCount);
    ImGui::Checkbox("Use Entity Camera", &bEntityCamera);
}
} // namespace ya