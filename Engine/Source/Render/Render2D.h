
#pragma once

#include "glm/glm.hpp"

#include "Core/App/App.h"
#include "Core/Base.h"
#include "Platform/Render/Vulkan/VulkanPipeline.h"
#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Platform/Render/Vulkan/VulkanRenderPass.h"



namespace ya
{

struct Render2D
{

    VulkanRenderPass *renderpass;
    VulkanPipeline   *pipeline;

    Render2D()          = default;
    virtual ~Render2D() = default;


    void init()
    {

        Neon::App *app      = Neon::App::get();
        auto      *vkRender = dynamic_cast<VulkanRender *>(app->getRender());
        renderpass          = new VulkanRenderPass(vkRender);

        renderpass->create(RenderPassCreateInfo{
            .attachments = {
                AttachmentDescription{
                    .format                  = EFormat::R8G8B8A8_UNORM,
                    .samples                 = ESampleCount::Sample_1,
                    .loadOp                  = EAttachmentLoadOp::Clear,
                    .storeOp                 = EAttachmentStoreOp::Store,
                    .stencilLoadOp           = EAttachmentLoadOp::DontCare,
                    .stencilStoreOp          = EAttachmentStoreOp::DontCare,
                    .bInitialLayoutUndefined = true,
                    .bFinalLayoutPresentSrc  = true, // for color attachments
                },
                // AttachmentDescription{
                //     .format                  = EFormat::D32_SFLOAT,
                //     .samples                 = ESampleCount::Sample_1,
                //     .loadOp                  = EAttachmentLoadOp::Clear,
                //     .storeOp                 = EAttachmentStoreOp::DontCare,
                //     .stencilLoadOp           = EAttachmentLoadOp::DontCare,
                //     .stencilStoreOp          = EAttachmentStoreOp::DontCare,
                //     .bInitialLayoutUndefined = true,
                //     .bFinalLayoutPresentSrc  = false, // for depth attachments
                // },
            },
            .subpasses = {
                RenderPassCreateInfo::SubpassInfo{
                    .subpassIndex = 0,
                    // .inputAttachments = {
                    //     RenderPassCreateInfo::AttachmentRef{
                    //         .ref    = 0, // color attachment
                    //         .layout = EImageLayout::ColorAttachmentOptimal,
                    //     },
                    // },
                    .colorAttachments = {
                        RenderPassCreateInfo::AttachmentRef{
                            .ref    = 0, // color attachment
                            .layout = EImageLayout::ColorAttachmentOptimal,
                        },
                    },
                },
            },
            .dependencies = {
                RenderPassCreateInfo::SubpassDependency{
                    .bSrcExternal = true,
                    .srcSubpass   = 0,
                    .dstSubpass   = 0,
                },
            },
        });

        pipeline = new VulkanPipeline(vkRender, renderpass, app->defaultPipelineLayout);
        pipeline->recreate(GraphicsPipelineCreateInfo{
            // .pipelineLayout   = pipelineLayout,
            .shaderCreateInfo = ShaderCreateInfo{
                .shaderName        = "Sprite2D.glsl",
                .bDeriveFromShader = false,
                .vertexBufferDescs = {
                    VertexBufferDescription{
                        .slot  = 0,
                        .pitch = static_cast<uint32_t>(T2Size(EVertexAttributeFormat::Float3)),
                    },
                },
                .vertexAttributes = {
                    // (layout binding = 0, set = 0) in vec3 aPos,
                    VertexAttribute{
                        .location   = 0,
                        .bufferSlot = 0,
                        .format     = EVertexAttributeFormat::Float3,
                        .offset     = 0,
                    },
                    // (layout binding = 1, set = 0) in vec4 aColor,
                    VertexAttribute{
                        .location   = 1,
                        .bufferSlot = 0, // same buffer slot
                        .format     = EVertexAttributeFormat::Float4,
                        .offset     = static_cast<uint32_t>(T2Size(EVertexAttributeFormat::Float4)),
                    },

                },
            },
            .subPassRef = 0,
            // define what state need to dynamically modified in render pass execution
            .dynamicFeatures    = {},
            .primitiveType      = EPrimitiveType::TriangleList,
            .rasterizationState = RasterizationState{
                .polygonMode = EPolygonMode::Fill,
                .frontFace   = EFrontFaceType::CounterClockWise, // GL
            },
            .multisampleState  = MultisampleState{},
            .depthStencilState = DepthStencilState{},
            .colorBlendState   = ColorBlendState{
                  .bLogicOpEnable = false,
                  .attachments    = {
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
            .viewportState = ViewportState{},

        });
    }

    void destroy()
    {
    }

    // 渲染方法
    virtual void begin() = 0;
    virtual void end()   = 0;

    // 绘制矩形
    virtual void drawRect(const glm::vec2 &position, const glm::vec2 &size, const glm::vec4 &color) = 0;

    // 绘制文本
    virtual void drawText(const std::string &text, const glm::vec2 &position, const glm::vec4 &color) = 0;

    // 设置视口
    virtual void setViewport(int x, int y, int width, int height) = 0;
};

}; // namespace ya