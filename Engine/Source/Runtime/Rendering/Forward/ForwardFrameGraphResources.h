#pragma once

#include "Core/Math/Geometry.h"
#include "Render/Core/Graph/RenderGraph.h"
#include "Runtime/Rendering/Common/RenderOverlay.h"
#include "Runtime/Rendering/Forward/ForwardViewportAuxPasses.h"

#include <memory>

namespace ya
{

namespace forward_graph_exports
{

inline constexpr std::string_view viewportColor   = "ForwardViewport.Color";
inline constexpr std::string_view viewportDepth   = "ForwardViewport.Depth";
inline constexpr std::string_view viewportResolve = "ForwardViewport.Resolve";
inline constexpr std::string_view entityId        = "ForwardViewport.EntityId";

} // namespace forward_graph_exports

struct ICommandBuffer;

/// Typed parameters for the Forward opaque graph pass.
struct ForwardOpaquePassParams
{
    RGTextureHandle     viewportColor{};
    RGTextureHandle     viewportDepth{};
    Rect2D              renderArea{};
    uint32_t            layerCount = 1;
    EImageLayout::T     finalLayout = EImageLayout::ColorAttachmentOptimal;
    std::vector<ForwardDirectionGizmoInput> directionGizmos{};
};

/// Typed parameters for the Forward skybox graph pass.
struct ForwardSkyboxPassParams
{
    RGTextureHandle     viewportColor{};
    RGTextureHandle     viewportDepth{};
    Rect2D              renderArea{};
    uint32_t            layerCount = 1;
    EImageLayout::T     finalLayout = EImageLayout::ColorAttachmentOptimal;
};

/// Typed parameters for the Forward entity-id pick graph pass.
///
/// The pass renders every draw item's entity id into an R32_UINT target,
/// depth-tested against the already-written viewport depth, so a cursor
/// readback selects exactly what is visible.
struct ForwardEntityIdPassParams
{
    RGTextureHandle viewportColor{};
    RGTextureHandle viewportDepth{};
    Rect2D          renderArea{};
    uint32_t        layerCount = 1;
    EImageLayout::T finalLayout = EImageLayout::ColorAttachmentOptimal;
};

/// Typed parameters for the Forward transparent graph pass.
struct ForwardTransparentPassParams
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
struct ForwardOverlayPassParams
{
    RGTextureHandle     viewportColor{};
    RGTextureHandle     viewportDepth{};
    Rect2D              renderArea{};
    uint32_t            layerCount = 1;
    EImageLayout::T     finalLayout = EImageLayout::ColorAttachmentOptimal;
    std::shared_ptr<const RenderViewportOverlaySnapshot> overlaySnapshot = nullptr;
    FrameContext        frameCtx{};
};

} // namespace ya
