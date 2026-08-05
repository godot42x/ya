#pragma once

#include "Core/Math/Geometry.h"
#include "Render/Core/Graph/RenderGraph.h"
#include "Runtime/Rendering/Forward/ForwardViewportAuxPasses.h"

#include <memory>

namespace ya
{

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
