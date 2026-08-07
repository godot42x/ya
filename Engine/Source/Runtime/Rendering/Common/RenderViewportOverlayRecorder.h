#pragma once

#include "Runtime/Rendering/Common/RenderOverlay.h"

#include <memory>

namespace ya
{

struct FrameContext;
struct ICommandBuffer;
struct Node;

void recordRenderViewportOverlayPass(const FrameContext& frameCtx,
                                     const std::shared_ptr<const RenderViewportOverlaySnapshot>& overlaySnapshot,
                                     ICommandBuffer* cmdBuf);

/// Record the game UI (Node2D subtree of the scene tree) into the active
/// Render2D batch inside a dedicated UI pass.
void recordRenderUIPass(Node* uiSceneRoot, ICommandBuffer* cmdBuf, const Extent2D& viewportExtent);

} // namespace ya
