
#pragma once

#include "Render/Core/IRenderTarget.h"
#include "Render/Core/PipelineBuilder.h"
#include "Render/Material/MaterialDescPool.h"
#include "Render/Material/PhongMaterial.h"
#include "Render/Render.h"

#include "DeferredRender.GBufferPass.slang.h"
// #include "DeferredRender.LightPass.glsl.h"
namespace ya
{



struct DeferredRenderPipeline
{
    IRender* _render;

    stdptr<IRenderTarget> _gBufferRT;

    const EFormat::T COLOR_FORMAT = EFormat::R8G8B8A8_SRGB;

    stdptr<IGraphicsPipeline>                 _gBufferPipeline;
    stdptr<IPipelineLayout>                   _gBufferPipelineLayout;
    std::vector<stdptr<IDescriptorSetLayout>> _gBufferDSLs;

    using GBufferParamsData = slang_types::DeferredRender::GBufferPass::ParamsData;
    MaterialDescPool<PhongMaterial, GBufferParamsData> _matPool;

  public:
    void init()
    {
        Extent2D extent{
            .width  = _render->getSwapchainWidth(),
            .height = _render->getSwapchainHeight(),
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
                    .format         = EFormat::D32_SFLOAT,
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
                    .depthAttachmentFormat  = EFormat::D32_SFLOAT,
                },
                .depthStencilState = DepthStencilState{
                    .bDepthTestEnable  = true,
                    .bDepthWriteEnable = true,
                    .depthCompareOp    = ECompareOp::Less,
                },
            });
        _gBufferPipeline       = result.pipeline;
        _gBufferPipelineLayout = result.pipelineLayout;
        _gBufferDSLs           = result.descriptorSetLayouts;

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
    }

    void tick(ICommandBuffer* cmdBuf);
    void renderGUI();



  private:
};
} // namespace ya