#pragma once

#include "Graph/RenderGraph.h"

namespace ya::composition
{

/// Declares a raster pass that continues the View's color stream in place:
/// color and depth are Loaded (depth optionally test-only) and both stay
/// shader-readable for the passes after it. This is the single definition of
/// the "color continuation" contract shared by the skybox, sprite and overlay
/// passes and by the Deferred forward-opaque slot.
///
/// A pass that writes a different target, clears the color or changes the
/// final layouts is not a continuation and declares its own attachments.
inline void declareColorContinuation(RGPassBuilder&  passBuilder,
                                     const Rect2D&   renderArea,
                                     RGTextureHandle color,
                                     RGTextureHandle depth,
                                     bool            bDepthReadOnly = false)
{
    passBuilder.declareRaster({
        .renderArea = renderArea,
        .layerCount = 1,
        .colors = {{
            .color       = color,
            .loadOp      = EAttachmentLoadOp::Load,
            .storeOp     = EAttachmentStoreOp::Store,
            .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
        }},
        .depth = RGDepthAttachmentDesc{
            .depth       = depth,
            .loadOp      = EAttachmentLoadOp::Load,
            .storeOp     = EAttachmentStoreOp::Store,
            .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
            .bReadOnly   = bDepthReadOnly,
        },
    });
}

} // namespace ya::composition
