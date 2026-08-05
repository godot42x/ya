#pragma once

#include "Core/Math/Geometry.h"
#include "Render/Core/RenderGraph.h"

#include <functional>

namespace ya
{

struct ICommandBuffer;

/// Typed parameters for the Forward skybox graph pass (also clears the
/// viewport since it is the first pass in the Forward graph).
struct ForwardSkyboxPassParams
{
    RGTextureHandle     viewportColor{};
    RGTextureHandle     viewportDepth{};
    Rect2D              renderArea{};
    uint32_t            layerCount = 1;
    EImageLayout::T     finalLayout = EImageLayout::ColorAttachmentOptimal;
};

/// Typed parameters for the Forward PBR graph pass.
struct ForwardPBRPassParams
{
    RGTextureHandle     viewportColor{};
    RGTextureHandle     viewportDepth{};
    Rect2D              renderArea{};
    uint32_t            layerCount = 1;
    EImageLayout::T     finalLayout = EImageLayout::ColorAttachmentOptimal;
};

/// Typed parameters for the Forward Phong graph pass.
struct ForwardPhongPassParams
{
    RGTextureHandle     viewportColor{};
    RGTextureHandle     viewportDepth{};
    Rect2D              renderArea{};
    uint32_t            layerCount = 1;
    EImageLayout::T     finalLayout = EImageLayout::ColorAttachmentOptimal;
};

/// Typed parameters for the remaining Forward viewport passes
/// (Unlit / Simple / Direction / Debug + editor overlays). This is the last
/// Forward pass, so it owns the MSAA resolve attachment.
struct ForwardRestPassParams
{
    RGTextureHandle     viewportColor{};
    RGTextureHandle     viewportDepth{};
    Rect2D              renderArea{};
    uint32_t            layerCount = 1;
    EImageLayout::T     finalLayout = EImageLayout::ColorAttachmentOptimal;
    std::function<void(ICommandBuffer*, Extent2D)> recordViewportOverlays{};
};

} // namespace ya
