#pragma once

#include "Runtime/Rendering/Common/RenderOverlay.h"

#include <memory>

namespace ya
{

struct FrameContext;
struct ICommandBuffer;
struct Node;
struct RenderImage;
struct Extent2D;

void recordRenderViewportOverlayPass(const FrameContext& frameCtx,
                                     const std::shared_ptr<const RenderViewportOverlaySnapshot>& overlaySnapshot,
                                     ICommandBuffer* cmdBuf);

/// Composite the game UI (Node2D subtree of the scene tree) onto the FINAL
/// viewport image, after the world graph and its post-processing. Runs
/// graph-external with manual layout transitions, so UI never enters bloom or
/// tonemapping. UI coordinates are authored in logical viewport pixels and
/// mapped to the render-target pixels (frame buffer scale).
/// When `bDrawCanvasGrid` is set the target is cleared to a dark canvas color
/// and a grid is drawn behind the UI (editor 2D preview mode).
void recordUICompositorPass(ICommandBuffer*    cmdBuf,
                            RenderImage&       target,
                            const Extent2D&    logicalViewportExtent,
                            Node*              uiSceneRoot,
                            bool               bDrawCanvasGrid = false);

} // namespace ya
