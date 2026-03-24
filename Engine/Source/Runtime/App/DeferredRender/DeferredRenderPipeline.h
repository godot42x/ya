
#pragma once

#include "Core/Math/Geometry.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/IRenderTarget.h"
#include "Render/Core/Pipeline.h"
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

    const EFormat::T        COLOR_FORMAT = EFormat::R8G8B8A8_SRGB;
    const EFormat::T        DEPTH_FORMAT = EFormat::D32_SFLOAT;
    stdptr<IDescriptorPool> _deferredDSP;

    stdptr<IGraphicsPipeline> _gBufferPipeline;

    stdptr<IPipelineLayout>      _gBufferPPL;
    stdptr<IDescriptorSetLayout> _frameAndLightDSL;
    stdptr<IDescriptorSetLayout> _resourceOrLightTexturesDSL;
    stdptr<IDescriptorSetLayout> _paramsDSL;


    stdptr<IGraphicsPipeline> _lightPipeline;
    stdptr<IPipelineLayout>   _lightPPL;

    DescriptorSetHandle _frameAndLightDS;
    DescriptorSetHandle _lightTexturesDS;

    stdptr<IBuffer> _frameUBO = nullptr;
    stdptr<IBuffer> _lightUBO = nullptr;


    // GBuffer pass types
    // using GBufferFrameData    = slang_types::DeferredRender::GBufferPass::FrameData;
    // using GBufferLightData    = slang_types::DeferredRender::GBufferPass::LightData;
    using GBufferPushConstant = slang_types::DeferredRender::GBufferPass::PushConstants;

    // Light pass types
    using LightPassFrameData    = slang_types::DeferredRender::LightPass::FrameData;
    using LightPassLightData    = slang_types::DeferredRender::LightPass::LightData;
    using LightPassPushConstant = slang_types::DeferredRender::LightPass::PushConstants;
    LightPassFrameData uLightPassFrameData{};
    LightPassLightData uLightPassLightData{};

    // use one matPool, pipeline layout for two pass
    using ParamsData = slang_types::DeferredRender::GBufferPass::ParamsData;
    MaterialDescPool<PhongMaterial, ParamsData> _matPool;


    std::vector<VertexAttribute> _commonVertexAttributes = {
        VertexAttribute{
            .bufferSlot = 0,
            .location   = 0,
            .format     = EVertexAttributeFormat::Float3,

            .offset = offsetof(ya::Vertex, position),
        },
        VertexAttribute{
            .bufferSlot = 0,
            .location   = 1,
            .format     = EVertexAttributeFormat::Float2,
            .offset     = offsetof(ya::Vertex, texCoord0),
        },
        VertexAttribute{
            .bufferSlot = 0,
            .location   = 2,
            .format     = EVertexAttributeFormat::Float3,
            .offset     = offsetof(ya::Vertex, normal),
        },
        VertexAttribute{
            .bufferSlot = 0,
            .location   = 3,
            .format     = EVertexAttributeFormat::Float3,
            .offset     = offsetof(ya::Vertex, tangent),
        },
    };

    std::vector<DescriptorSetLayoutDesc> _commonDescriptorSetLayouts = {
        DescriptorSetLayoutDesc{
            .label    = "Deferred_GBuffer_Frame_And_Light_DSL",
            .set      = 0,
            .bindings = {
                DescriptorSetLayoutBinding{
                    .binding         = 0,
                    .descriptorType  = EPipelineDescriptorType::UniformBuffer,
                    .descriptorCount = 1,
                    .stageFlags      = EShaderStage::All,
                },
                DescriptorSetLayoutBinding{
                    .binding         = 1,
                    .descriptorType  = EPipelineDescriptorType::UniformBuffer,
                    .descriptorCount = 1,
                    .stageFlags      = EShaderStage::All,
                },
            },
        },
        DescriptorSetLayoutDesc{
            .label    = "Deferred_GBuffer_MaterialResources_DSL",
            .set      = 1,
            .bindings = {
                DescriptorSetLayoutBinding{
                    .binding         = 0,
                    .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
                    .descriptorCount = 1,
                    .stageFlags      = EShaderStage::All,
                },
                DescriptorSetLayoutBinding{
                    .binding         = 1,
                    .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
                    .descriptorCount = 1,
                    .stageFlags      = EShaderStage::All,
                },
                DescriptorSetLayoutBinding{
                    .binding         = 2,
                    .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
                    .descriptorCount = 1,
                    .stageFlags      = EShaderStage::All,
                },
            },
        },
        DescriptorSetLayoutDesc{
            .label    = "Deferred_GBuffer_MaterialParams_DSL",
            .set      = 2,
            .bindings = {
                DescriptorSetLayoutBinding{
                    .binding         = 0,
                    .descriptorType  = EPipelineDescriptorType::UniformBuffer,
                    .descriptorCount = 1,
                    .stageFlags      = EShaderStage::Fragment,
                },
            },
        },
    };


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

        auto commonDSLs = IDescriptorSetLayout::create(
            _render,
            _commonDescriptorSetLayouts);
        _frameAndLightDSL           = commonDSLs[0];
        _resourceOrLightTexturesDSL = commonDSLs[1];
        _paramsDSL                  = commonDSLs[2];

        _gBufferPPL = IPipelineLayout::create(
            _render,
            "GBuffer PPL",
            {
                PushConstantRange{
                    .offset     = 0,
                    .size       = sizeof(GBufferPushConstant),
                    .stageFlags = EShaderStage::Vertex,
                },
            },
            commonDSLs);

        GraphicsPipelineCreateInfo gBufferPipelineCI{
            .renderPass            = _gBufferRT->getRenderPass(),
            .pipelineRenderingInfo = PipelineRenderingInfo{
                .label                  = "GBuffer Pass",
                .colorAttachmentFormats = {COLOR_FORMAT, COLOR_FORMAT, COLOR_FORMAT},
                .depthAttachmentFormat  = DEPTH_FORMAT,
            },
            .pipelineLayout = _gBufferPPL.get(),
            .shaderDesc     = ShaderDesc{
                    .shaderName        = "DeferredRender/GBufferPass.slang",
                    .bDeriveFromShader = false,
                    .vertexBufferDescs = {
                    VertexBufferDescription{
                            .slot  = 0,
                            .pitch = sizeof(ya::Vertex),
                    },
                },
                    .vertexAttributes = _commonVertexAttributes,
            },
            .dynamicFeatures = {
                EPipelineDynamicFeature::Viewport,
                EPipelineDynamicFeature::Scissor,
            },
            .primitiveType      = EPrimitiveType::TriangleList,
            .rasterizationState = RasterizationState{
                .cullMode  = ECullMode::Back,
                .frontFace = EFrontFaceType::CounterClockWise,
            },
            .multisampleState  = MultisampleState{},
            .depthStencilState = DepthStencilState{
                .bDepthTestEnable  = true,
                .bDepthWriteEnable = true,
                .depthCompareOp    = ECompareOp::Less,
            },
            .colorBlendState = ColorBlendState{
                .attachments = {
                    ColorBlendAttachmentState{
                        .index               = 0,
                        .bBlendEnable        = false,
                        .srcColorBlendFactor = EBlendFactor::SrcAlpha,
                        .dstColorBlendFactor = EBlendFactor::OneMinusSrcAlpha,
                        .colorBlendOp        = EBlendOp::Add,
                        .srcAlphaBlendFactor = EBlendFactor::SrcAlpha,
                        .dstAlphaBlendFactor = EBlendFactor::OneMinusSrcAlpha,
                        .alphaBlendOp        = EBlendOp::Add,
                        .colorWriteMask      = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A,
                    },
                    ColorBlendAttachmentState{
                        .index               = 2,
                        .bBlendEnable        = false,
                        .srcColorBlendFactor = EBlendFactor::SrcAlpha,
                        .dstColorBlendFactor = EBlendFactor::OneMinusSrcAlpha,
                        .colorBlendOp        = EBlendOp::Add,
                        .srcAlphaBlendFactor = EBlendFactor::SrcAlpha,
                        .dstAlphaBlendFactor = EBlendFactor::OneMinusSrcAlpha,
                        .alphaBlendOp        = EBlendOp::Add,
                        .colorWriteMask      = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A,
                    },
                    ColorBlendAttachmentState{
                        .index               = 1,
                        .bBlendEnable        = false,
                        .srcColorBlendFactor = EBlendFactor::SrcAlpha,
                        .dstColorBlendFactor = EBlendFactor::OneMinusSrcAlpha,
                        .colorBlendOp        = EBlendOp::Add,
                        .srcAlphaBlendFactor = EBlendFactor::SrcAlpha,
                        .dstAlphaBlendFactor = EBlendFactor::OneMinusSrcAlpha,
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
        };
        _gBufferPipeline = IGraphicsPipeline::create(_render);
        YA_CORE_ASSERT(_gBufferPipeline && _gBufferPipeline->recreate(gBufferPipelineCI), "Failed to create deferred gbuffer pipeline");

        const uint32_t texCount = static_cast<uint32_t>(_resourceOrLightTexturesDSL->getLayoutInfo().bindings.size());
        _matPool.init(
            _render,
            _paramsDSL,
            _resourceOrLightTexturesDSL,
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


        _lightPPL = IPipelineLayout::create(
            _render,
            "Deferred Light PPL",
            {
                PushConstantRange{
                    .offset     = 0,
                    .size       = sizeof(LightPassPushConstant),
                    .stageFlags = EShaderStage::Vertex,
                },
            },
            commonDSLs);

        GraphicsPipelineCreateInfo lightPipelineCI{
            .renderPass            = nullptr,
            .pipelineRenderingInfo = PipelineRenderingInfo{
                .label                  = "Light Pass",
                .colorAttachmentFormats = {COLOR_FORMAT},
                .depthAttachmentFormat  = DEPTH_FORMAT,
            },
            .pipelineLayout = _lightPPL.get(),
            .shaderDesc     = ShaderDesc{

                .shaderName        = "DeferredRender/LightPass.slang",
                .bDeriveFromShader = false,
                .vertexBufferDescs = {
                    VertexBufferDescription{
                        .slot  = 0,
                        .pitch = sizeof(ya::Vertex),
                    },
                },
                .vertexAttributes = _commonVertexAttributes,
            },
            .dynamicFeatures = {
                EPipelineDynamicFeature::Viewport,
                EPipelineDynamicFeature::Scissor,
            },
            .primitiveType      = EPrimitiveType::TriangleList,
            .rasterizationState = RasterizationState{
                .cullMode  = ECullMode::None,
                .frontFace = EFrontFaceType::CounterClockWise,
            },
            .multisampleState  = MultisampleState{},
            .depthStencilState = DepthStencilState{
                .bDepthTestEnable  = true,
                .bDepthWriteEnable = false,
            },
            .colorBlendState = ColorBlendState{
                .attachments = {
                    ColorBlendAttachmentState{
                        .index               = 0,
                        .bBlendEnable        = false,
                        .srcColorBlendFactor = EBlendFactor::SrcAlpha,
                        .dstColorBlendFactor = EBlendFactor::OneMinusSrcAlpha,
                        .colorBlendOp        = EBlendOp::Add,
                        .srcAlphaBlendFactor = EBlendFactor::SrcAlpha,
                        .dstAlphaBlendFactor = EBlendFactor::OneMinusSrcAlpha,
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
        };
        _lightPipeline = IGraphicsPipeline::create(_render);
        YA_CORE_ASSERT(_lightPipeline && _lightPipeline->recreate(lightPipelineCI), "Failed to create deferred light pipeline");

        _deferredDSP = IDescriptorPool::create(
            _render,
            DescriptorPoolCreateInfo{
                .label     = "Deferred Rendering DSP",
                .maxSets   = 2,
                .poolSizes = {
                    DescriptorPoolSize{
                        .type            = EPipelineDescriptorType::UniformBuffer,
                        .descriptorCount = 2, // frame + light
                    },
                    DescriptorPoolSize{
                        .type            = EPipelineDescriptorType::CombinedImageSampler,
                        .descriptorCount = 3,
                    },
                },
            });

        _frameAndLightDS = _deferredDSP->allocateDescriptorSets(_frameAndLightDSL);
        _lightTexturesDS = _deferredDSP->allocateDescriptorSets(_resourceOrLightTexturesDSL);

        _frameUBO = IBuffer::create(
            _render,
            BufferCreateInfo{
                .label         = "Deferrred frame buffer",
                .usage         = EBufferUsage::UniformBuffer,
                .size          = sizeof(LightPassFrameData),
                .memProperties = EMemoryProperty::HostVisible | EMemoryProperty::HostCoherent,
            });

        _lightUBO = IBuffer::create(
            _render,
            BufferCreateInfo{
                .label         = "Deferrred light buffer",
                .usage         = EBufferUsage::UniformBuffer,
                .size          = sizeof(LightPassLightData),
                .memProperties = EMemoryProperty::HostVisible | EMemoryProperty::HostCoherent,
            });


        _render->getDescriptorHelper()->updateDescriptorSets({
            IDescriptorSetHelper::writeOneUniformBuffer(_frameAndLightDS, 0, _frameUBO.get()),
            IDescriptorSetHelper::writeOneUniformBuffer(_frameAndLightDS, 1, _lightUBO.get()),
        });
    }

    struct TickDesc
    {
        ICommandBuffer*         cmdBuf;
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



  private:
};
} // namespace ya