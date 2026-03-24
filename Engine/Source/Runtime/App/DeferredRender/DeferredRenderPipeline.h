
#pragma once

#include "Render/Core/IRenderTarget.h"
#include "Render/Core/PipelineBuilder.h"
#include "Render/Material/MaterialDescPool.h"
#include "Render/Material/PhongMaterial.h"
#include "Render/Render.h"

#include "DeferredRender.GBufferPass.slang.h"
#include "DeferredRender.LightPass.slang.h"
#include "Render/RenderDefines.h"

namespace ya
{



struct DeferredRenderPipeline
{
    IRender* _render;

    stdptr<IRenderTarget> _gBufferRT;
    stdptr<IRenderTarget> _viewportRT;

    const EFormat::T COLOR_FORMAT = EFormat::R8G8B8A8_SRGB;
    const EFormat::T DEPTH_FORMAT = EFormat::D32_SFLOAT;

    stdptr<IGraphicsPipeline>                 _gBufferPipeline;
    stdptr<IPipelineLayout>                   _gBufferPPL;
    std::vector<stdptr<IDescriptorSetLayout>> _gBufferDSLs;

    // GBuffer pass types
    using GBufferFrameData    = slang_types::DeferredRender::GBufferPass::FrameData;
    using GBufferLightData    = slang_types::DeferredRender::GBufferPass::LightData;
    using GBufferPushConstant = slang_types::DeferredRender::GBufferPass::PushConstants;

    // Light pass types
    using LightPassFrameData = slang_types::DeferredRender::LightPass::FrameData;
    using LightPassLightData = slang_types::DeferredRender::LightPass::LightData;
    LightPassLightData uLightPassLightData{};

    // use one matPool, pipeline layout for two pass
    using ParamsData = slang_types::DeferredRender::GBufferPass::ParamsData;
    MaterialDescPool<PhongMaterial, ParamsData> _matPool;


    stdptr<IGraphicsPipeline>                 _lightPipeline;
    stdptr<IPipelineLayout>                   _lightPPL;
    std::vector<stdptr<IDescriptorSetLayout>> _lightDSLs;

    PipelineLayoutDesc _basePPL = PipelineLayoutDesc{
        .label                = "Deferred Common PPL",
        .descriptorSetLayouts = {
            DescriptorSetLayoutDesc{
                .label = "Material Resources",
                // .bindings = {
                //     // textures
                //     DescriptorSetLayoutBinding{
                //         .binding = 0,
                //         .type    = EPipelineDescriptorType::CombinedImageSampler,
                //         .count   = 4, // albedo + normal + specular + depth
                //         .stage   = EShaderStage::Fragment,
                //     },
                // },
            },
            DescriptorSetLayoutDesc{
                .label    = "Material Params",
                .bindings = {
                    DescriptorSetLayoutBinding{
                        .binding        = 0,
                        .descriptorType = EPipelineDescriptorType::UniformBuffer,
                        .stageFlags     = EShaderStage::Fragment,
                    },
                },
            },

        }};

  public:

    struct InitDesc
    {
        IRender* render  = nullptr;
        int      windowW = 0;
        int      windowH = 0;
    };

    void init(const InitDesc& desc)
    {
        _render = desc.render;

        // by the viewport size later
        Extent2D extent{
            .width  = static_cast<uint32_t>(desc.windowW),
            .height = static_cast<uint32_t>(desc.windowH),
        };

        _gBufferRT = createRenderTarget(RenderTargetCreateInfo{
            .label            = "GBuffer RenderTarget",
            .renderingMode    = ERenderingMode::DynamicRendering,
            .bSwapChainTarget = false,
            .extent           = extent,
            .frameBufferCount = 1,

            .attachments = {
                .colorAttach = {
                    //  geometry
                    AttachmentDescription{
                        .index          = 0,
                        .format         = COLOR_FORMAT,
                        .samples        = ESampleCount::Sample_1,
                        .loadOp         = EAttachmentLoadOp::Clear,
                        .storeOp        = EAttachmentStoreOp::Store,
                        .stencilLoadOp  = EAttachmentLoadOp::DontCare,
                        .stencilStoreOp = EAttachmentStoreOp::DontCare,
                        .initialLayout  = EImageLayout::ColorAttachmentOptimal,
                        .finalLayout    = EImageLayout::ShaderReadOnlyOptimal,
                        .usage          = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                    },
                    // normal
                    AttachmentDescription{
                        .index          = 1,
                        .format         = COLOR_FORMAT,
                        .samples        = ESampleCount::Sample_1,
                        .loadOp         = EAttachmentLoadOp::Clear,
                        .storeOp        = EAttachmentStoreOp::Store,
                        .stencilLoadOp  = EAttachmentLoadOp::DontCare,
                        .stencilStoreOp = EAttachmentStoreOp::DontCare,
                        .initialLayout  = EImageLayout::ColorAttachmentOptimal,
                        .finalLayout    = EImageLayout::ShaderReadOnlyOptimal,
                        .usage          = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                    },
                    // color + specular(w is specular)
                    AttachmentDescription{
                        .index          = 2,
                        .format         = COLOR_FORMAT,
                        .samples        = ESampleCount::Sample_1,
                        .loadOp         = EAttachmentLoadOp::Clear,
                        .storeOp        = EAttachmentStoreOp::Store,
                        .stencilLoadOp  = EAttachmentLoadOp::DontCare,
                        .stencilStoreOp = EAttachmentStoreOp::DontCare,
                        .initialLayout  = EImageLayout::ColorAttachmentOptimal,
                        .finalLayout    = EImageLayout::ShaderReadOnlyOptimal,
                        .usage          = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                    },

                },
                .depthAttach = AttachmentDescription{
                    .index          = 3,
                    .format         = DEPTH_FORMAT,
                    .samples        = ESampleCount::Sample_1,
                    .loadOp         = EAttachmentLoadOp::Clear,
                    .storeOp        = EAttachmentStoreOp::Store,
                    .stencilLoadOp  = EAttachmentLoadOp::DontCare,
                    .stencilStoreOp = EAttachmentStoreOp::DontCare,
                    .initialLayout  = EImageLayout::DepthStencilAttachmentOptimal,
                    .finalLayout    = EImageLayout::DepthStencilAttachmentOptimal,
                    .usage          = EImageUsage::DepthStencilAttachment,
                },
            },
        });

        auto result = PipelineBuilder::create(
            _render,
            PipelineBuilder::CreateDesc{
                .shaderName            = "DeferredRender/GBufferPass.slang",
                .renderTarget          = _gBufferRT.get(),
                .pipelineRenderingInfo = PipelineRenderingInfo{
                    .label                  = "GBuffer Pass",
                    .colorAttachmentFormats = {COLOR_FORMAT, COLOR_FORMAT, COLOR_FORMAT},
                    .depthAttachmentFormat  = DEPTH_FORMAT,
                },
                .rasterizationState = RasterizationState{
                    .cullMode  = ECullMode::Back,
                    .frontFace = EFrontFaceType::CounterClockWise,
                },
                .depthStencilState = DepthStencilState{
                    .bDepthTestEnable  = true,
                    .bDepthWriteEnable = true,
                    .depthCompareOp    = ECompareOp::Less,
                },
                .multisampleState = MultisampleState{},
                .colorBlendState  = ColorBlendState{
                     .attachments = {
                        ColorBlendAttachmentState{
                            // index of the attachments in the render pass and the renderpass begin info
                             .index               = 0,
                             .bBlendEnable        = false,                          // no need for deferred render?
                             .srcColorBlendFactor = EBlendFactor::SrcAlpha,         // srcColor = srcColor * srcAlpha
                             .dstColorBlendFactor = EBlendFactor::OneMinusSrcAlpha, // dstColor = dstColor * (1 - srcAlpha)
                             .colorBlendOp        = EBlendOp::Add,                  // finalColor = srcColor + dstColor
                             .srcAlphaBlendFactor = EBlendFactor::SrcAlpha,         // use src alpha for alpha blending
                             .dstAlphaBlendFactor = EBlendFactor::OneMinusSrcAlpha, // use dst alpha for alpha blending
                             .alphaBlendOp        = EBlendOp::Add,
                             .colorWriteMask      = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A,
                        },
                        ColorBlendAttachmentState{
                            // index of the attachments in the render pass and the renderpass begin info
                             .index               = 2,
                             .bBlendEnable        = false,
                             .srcColorBlendFactor = EBlendFactor::SrcAlpha,         // srcColor = srcColor * srcAlpha
                             .dstColorBlendFactor = EBlendFactor::OneMinusSrcAlpha, // dstColor = dstColor * (1 - srcAlpha)
                             .colorBlendOp        = EBlendOp::Add,                  // finalColor = srcColor + dstColor
                             .srcAlphaBlendFactor = EBlendFactor::SrcAlpha,         // use src alpha for alpha blending
                             .dstAlphaBlendFactor = EBlendFactor::OneMinusSrcAlpha, // use dst alpha for alpha blending
                             .alphaBlendOp        = EBlendOp::Add,
                             .colorWriteMask      = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A,
                        },
                        ColorBlendAttachmentState{
                            // index of the attachments in the render pass and the renderpass begin info
                             .index               = 1,
                             .bBlendEnable        = false,
                             .srcColorBlendFactor = EBlendFactor::SrcAlpha,         // srcColor = srcColor * srcAlpha
                             .dstColorBlendFactor = EBlendFactor::OneMinusSrcAlpha, // dstColor = dstColor * (1 - srcAlpha)
                             .colorBlendOp        = EBlendOp::Add,                  // finalColor = srcColor + dstColor
                             .srcAlphaBlendFactor = EBlendFactor::SrcAlpha,         // use src alpha for alpha blending
                             .dstAlphaBlendFactor = EBlendFactor::OneMinusSrcAlpha, // use dst alpha for alpha blending
                             .alphaBlendOp        = EBlendOp::Add,
                             .colorWriteMask      = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A,
                        },
                    },
                },
                .viewportState = ViewportState{
                    .viewports = {
                        Viewport::defaults(),
                    },
                    .scissors = {
                        Scissor::defaults(),
                    },
                },
            });
        _gBufferPipeline = result.pipeline;
        _gBufferPPL      = result.pipelineLayout;
        _gBufferDSLs     = result.descriptorSetLayouts;

        // _gBufferDSLs[0] = set 1 (resource: textures), _gBufferDSLs[1] = set 2 (params UBO)
        const uint32_t texCount = static_cast<uint32_t>(_gBufferDSLs[0]->getLayoutInfo().bindings.size());
        _matPool.init(
            _render,
            _gBufferDSLs[1], // set 2: params UBO
            _gBufferDSLs[0], // set 1: textures
            [texCount](uint32_t n) {
                return std::vector<DescriptorPoolSize>{
                    {.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = n},
                    {.type = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = n * texCount},
                };
            },
            16);


        // MARK: viewport/light pass
        _viewportRT = createRenderTarget(RenderTargetCreateInfo{
            .label            = "Deferred RT",
            .bSwapChainTarget = false,
            .extent           = extent,
            .attachments      = {

                .colorAttach = {
                    AttachmentDescription{
                        .index          = 0,
                        .format         = COLOR_FORMAT,
                        .samples        = ESampleCount::Sample_1,
                        .loadOp         = EAttachmentLoadOp::Clear,
                        .storeOp        = EAttachmentStoreOp::Store,
                        .stencilLoadOp  = EAttachmentLoadOp::DontCare,
                        .stencilStoreOp = EAttachmentStoreOp::DontCare,
                        .initialLayout  = EImageLayout::ColorAttachmentOptimal,
                        .finalLayout    = EImageLayout::ShaderReadOnlyOptimal, // for swapchain output
                        .usage          = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                    },
                },
            },
        });

        auto lightPassResult = PipelineBuilder::create(
            _render,
            PipelineBuilder::CreateDesc{
                .shaderName = "DeferredRender/LightPass.slang",
                // here need to share depth rt from GBuffer pass for depth test, output dir
                //  but vulkan dynamic rendering does not support input attachment, so we read depth as texture in shader, and the depth attachment is not needed in light pass render target
                // .renderTarget          = viewportRt.get(),
                .pipelineRenderingInfo = PipelineRenderingInfo{
                    .label                  = "Light Pass",
                    .colorAttachmentFormats = {COLOR_FORMAT},
                    .depthAttachmentFormat  = DEPTH_FORMAT,
                },
                .rasterizationState = RasterizationState{
                    .cullMode  = ECullMode::None,
                    .frontFace = EFrontFaceType::CounterClockWise,
                },
                .depthStencilState = DepthStencilState{
                    .bDepthTestEnable  = true,
                    .bDepthWriteEnable = false,
                },
                .multisampleState = MultisampleState{},
                .colorBlendState  = ColorBlendState{
                     .attachments = {
                        ColorBlendAttachmentState{
                            // index of the attachments in the render pass and the renderpass begin info
                             .index               = 0,
                             .bBlendEnable        = false,                          // no need for deferred render?
                             .srcColorBlendFactor = EBlendFactor::SrcAlpha,         // srcColor = srcColor * srcAlpha
                             .dstColorBlendFactor = EBlendFactor::OneMinusSrcAlpha, // dstColor = dstColor * (1 - srcAlpha)
                             .colorBlendOp        = EBlendOp::Add,                  // finalColor = srcColor + dstColor
                             .srcAlphaBlendFactor = EBlendFactor::SrcAlpha,         // use src alpha for alpha blending
                             .dstAlphaBlendFactor = EBlendFactor::OneMinusSrcAlpha, // use dst alpha for alpha blending
                             .alphaBlendOp        = EBlendOp::Add,
                             .colorWriteMask      = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A,
                        },
                    },
                },
                .viewportState = ViewportState{
                    .viewports = {
                        Viewport::defaults(),
                    },
                    .scissors = {
                        Scissor::defaults(),
                    },
                },
                .defines = {},
            });


        _lightPipeline = result.pipeline;
        _lightPPL      = result.pipelineLayout;
        _lightDSLs     = result.descriptorSetLayouts;
    }

    struct TickDesc
    {
        ICommandBuffer*         cmdBuf                   = nullptr;
        float                   dt                       = 0.0f;
        glm::mat4               view                     = glm::mat4(1.0f);
        glm::mat4               projection               = glm::mat4(1.0f);
        glm::vec3               cameraPos                = glm::vec3(0.0f);
        Rect2D                  viewportRect             = {};
        float                   viewportFrameBufferScale = 1.0f;
        int                     appMode                  = 0;
        std::vector<glm::vec2>* clicked                  = nullptr;
    };
    void tick(const TickDesc& desc);
    void shutdown() {}
    void renderGUI();

    void fillLightDataFromFrameContext(const FrameContext* ctx);



  private:
};
} // namespace ya