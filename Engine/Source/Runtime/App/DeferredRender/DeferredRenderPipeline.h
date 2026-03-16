
#pragma once

#include "Render/Core/IRenderTarget.h"
#include "Render/Core/PipelineBuilder.h"
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

    // Shader-driven pipeline (auto-derived from GBufferPass.slang)
    PipelineBuilder::CreateDesc _gBufferCreateDesc;
    PipelineBuilder::Result     _gBufferPassResult;
    bool                        _bGBufferPipelineDirty = false;

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
    }

    void tick(ICommandBuffer* cmdBuf);
    void renderGUI();



  private:
    void createGBufferPipeline();
};
} // namespace ya