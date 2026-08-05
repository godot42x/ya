#pragma once

#include "Core/Math/Geometry.h"
#include "Render/Core/Graph/RenderGraph.h"
#include "Runtime/Rendering/Forward/ForwardViewportAuxPasses.h"

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

/// Typed parameters for the Forward Unlit graph pass.
struct ForwardUnlitPassParams
{
    RGTextureHandle     viewportColor{};
    RGTextureHandle     viewportDepth{};
    Rect2D              renderArea{};
    uint32_t            layerCount = 1;
    EImageLayout::T     finalLayout = EImageLayout::ColorAttachmentOptimal;
};

/// Typed parameters for the Forward Simple graph pass.
struct ForwardSimplePassParams
{
    RGTextureHandle     viewportColor{};
    RGTextureHandle     viewportDepth{};
    Rect2D              renderArea{};
    uint32_t            layerCount = 1;
    EImageLayout::T     finalLayout = EImageLayout::ColorAttachmentOptimal;
};

/// Typed parameters for the Forward Direction overlay graph pass. Direction
/// gizmos are a prebuilt frame snapshot (FG-704); the pass never queries the
/// ECS during execute.
struct ForwardDirectionPassParams
{
    RGTextureHandle     viewportColor{};
    RGTextureHandle     viewportDepth{};
    Rect2D              renderArea{};
    uint32_t            layerCount = 1;
    EImageLayout::T     finalLayout = EImageLayout::ColorAttachmentOptimal;
    std::vector<ForwardDirectionGizmoInput> directionGizmos{};
};

/// Typed parameters for the Forward Debug graph pass.
struct ForwardDebugPassParams
{
    RGTextureHandle     viewportColor{};
    RGTextureHandle     viewportDepth{};
    Rect2D              renderArea{};
    uint32_t            layerCount = 1;
    EImageLayout::T     finalLayout = EImageLayout::ColorAttachmentOptimal;
};

/// Typed parameters for the editor viewport overlay graph pass. This is the
/// last Forward pass, so it owns the MSAA resolve attachment and the final
/// consumer layout.
struct ForwardViewportOverlayPassParams
{
    RGTextureHandle     viewportColor{};
    RGTextureHandle     viewportDepth{};
    Rect2D              renderArea{};
    uint32_t            layerCount = 1;
    EImageLayout::T     finalLayout = EImageLayout::ColorAttachmentOptimal;
    std::function<void(ICommandBuffer*, Extent2D)> recordViewportOverlays{};
};

} // namespace ya
