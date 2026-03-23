
#pragma once

#include "Render/Core/IRenderTarget.h"
#include "Render/Core/PipelineBuilder.h"
#include "Render/Material/MaterialDescPool.h"
#include "Render/Material/PhongMaterial.h"
#include "Render/Render.h"

#include "DeferredRender.GBufferPass.slang.h"
#include "DeferredRender.LightPass.glsl.h"
namespace ya
{



struct DeferredRenderPipeline
{
    IRender* _render;

    stdptr<IRenderTarget> _gBufferRT;

    const EFormat::T COLOR_FORMAT = EFormat::R8G8B8A8_SRGB;
    const EFormat::T DEPTH_FORMAT = EFormat::D32_SFLOAT;

    stdptr<IGraphicsPipeline>                 _gBufferPipeline;
    stdptr<IPipelineLayout>                   _gBufferPPL;
    std::vector<stdptr<IDescriptorSetLayout>> _gBufferDSLs;

    // use one matPool, pipeline layout for two pass
    using GBufferParamsData = slang_types::DeferredRender::GBufferPass::ParamsData;
    MaterialDescPool<PhongMaterial, GBufferParamsData> _matPool;


    stdptr<IGraphicsPipeline>                 _lightPipeline;
    stdptr<IPipelineLayout>                   _lightPPL;
    std::vector<stdptr<IDescriptorSetLayout>> _lightDSLs;
    using LightPassParamsData = slang_types::DeferredRender::GBufferPass::ParamsData;
    // MaterialDescPool<PhongMaterial, LightPassParamsData> _lightMatPool;

  public:
    void init()
    {
        // by the viewport size later
        Extent2D extent{
            .width  = 800,
            .height = 600,
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
                        .initialLayout  = EImageLayout::Undefined,
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
                        .initialLayout  = EImageLayout::Undefined,
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
                        .initialLayout  = EImageLayout::Undefined,
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
                    .initialLayout  = EImageLayout::Undefined,
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
        auto viewportRt = createRenderTarget(RenderTargetCreateInfo{
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
            });
    }

    void tick(ICommandBuffer* cmdBuf);
    void renderGUI();



  private:
};
} // namespace ya