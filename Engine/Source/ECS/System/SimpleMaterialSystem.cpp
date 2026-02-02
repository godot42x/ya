#pragma once
#include "SimpleMaterialSystem.h"


#include "Core/App/App.h"

#include "ECS/Component.h"
#include "ECS/Component/CameraComponent.h"
#include "ECS/Component/MeshComponent.h"

#include "Core/Math/Geometry.h"
#include "Render/Core/IRenderTarget.h"

#include "Render/Core/Swapchain.h"
#include "Render/Mesh.h"


#include "vulkan/vulkan.h"

#include "ECS/Component/Material/SimpleMaterialComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/Entity.h"



#include "imgui.h"

namespace ya
{


void SimpleMaterialSystem::onInit(IRenderPass *renderPass)
{
    _label       = "SimpleMaterialSystem";
    auto *render = getRender();

    auto _sampleCount = ESampleCount::Sample_1;

    constexpr auto size = sizeof(SimpleMaterialSystem::PushConstant);
    YA_CORE_DEBUG("SimpleMaterialSystem PushConstant size: {}", size);
    PipelineDesc pipDesc{
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
        .subPassRef = 0,
        .renderPass = renderPass,
        // .pipelineLayout   = pipelineLayout,
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
    _pipeline = IGraphicsPipeline::create(render, _pipelineLayout.get());
    _pipeline->recreate(pipelineCI);
}

void SimpleMaterialSystem::onDestroy()
{
    _pipelineLayoutOwner.reset();
    _pipeline.reset();
    _pipelineLayout = nullptr;
}

void SimpleMaterialSystem::onUpdate(float deltaTime)
{
}

void SimpleMaterialSystem::onRender(ICommandBuffer *cmdBuf, IRenderTarget *rt)
{

    auto render = getRender();
    auto scene  = getActiveScene();
    if (!scene) {
        return;
    }
    const auto &view = scene->getRegistry().view<TransformComponent, SimpleMaterialComponent, MeshComponent>();
    if (view.begin() == view.end()) {
        return;
    }

    // Wrap void* in VulkanCommandBuffer for generic bind call
    _pipeline->bind(cmdBuf->getHandle());
    auto curFrameBuffer = rt->getFrameBuffer();

#pragma region Dynamic State
    // need set VkPipelineDynamicStateCreateInfo
    // or those properties should be modified in the pipeline recreation if needed.
    // but sometimes that we want to enable depth or color-blend state dynamically
    auto         fbExtent = curFrameBuffer->getExtent();
    ::VkViewport viewport{
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

    // Use cached camera context (updated once per frame in App::onUpdate)
    const auto &camCtx = rt->getFrameContext();
    pc.view            = camCtx.view;
    pc.projection      = camCtx.projection;

    for (const auto entity : view) {
        const auto &[tc, smc, meshComp] = view.get(entity);
        // TODO: culling works

        SimpleMaterial *material = smc.getMaterial();
        if (!material) {
            continue;
        }

        pc.model     = tc.getTransform();
        pc.colorType = material->colorType;

        // pc.colorType = sin(TimeManager::get()->now().time_since_epoch().count() * 0.001);

        cmdBuf->pushConstants(_pipelineLayout.get(),
                              EShaderStage::Vertex,
                              0,
                              sizeof(PushConstant),
                              &pc);

        // Draw single mesh from MeshComponent
        Mesh *mesh = meshComp.getMesh();
        if (mesh && mesh->getVertexBuffer() && mesh->getIndexBuffer()) {
            VkBuffer vertexBuffers[] = {mesh->getVertexBuffer()->getHandleAs<::VkBuffer>()};
            // current no need to support subbuffer
            VkDeviceSize offsets[] = {0};

            cmdBuf->bindVertexBuffer(0, mesh->getVertexBufferMut(), 0);
            cmdBuf->bindIndexBuffer(mesh->getIndexBufferMut(), 0, false);
            cmdBuf->drawIndexed(mesh->getIndexCount(), 1, 0, 0, 0);
        }
    }
}

} // namespace ya