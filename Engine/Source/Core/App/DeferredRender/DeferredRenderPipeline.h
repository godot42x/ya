
#pragma once

#include "Render/Core/IRenderTarget.h"
#include "Render/Render.h"

namespace ya
{



struct DeferredRenderPipeline
{
    IRender* _render;

    stdptr<IRenderTarget> _gBufferRT;

    const EFormat::T COLOR_FORMAT = EFormat::R8G8B8A8_SRGB;

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

            .attachments      = {
                //
                     .colorAttach = {
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
                },

                     .depthAttach = AttachmentDescription{
                         .index = 1,
                    //  .format         = DEPTH_FORMAT,
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

    void tick(ICommandBuffer* cmdBuf)
    {

        // 1. grab all mesh with * material, render to geometry
        // cmdBuf->beginRendering(RenderingInfo{
        //     .label      = "GBuffer Pass",
        //     .renderArea = Rect2D{
        //         .pos    = {0, 0},
        //         .extent = _gBuffer.geometry->getExtent().toVec2(),
        //     },
        //     .layerCount       = 1,
        //     .colorClearValues = {ClearValue(0.0f, 0.0f, 0.0f, 1.0f)},
        //     .depthClearValue  = ClearValue(1.0f, 0),
        //     .renderTarget     = _gBuffer.geometry.get(),
        // });
    }
};
} // namespace ya