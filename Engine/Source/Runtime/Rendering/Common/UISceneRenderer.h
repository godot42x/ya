#pragma once

#include "Core/Event.h"
#include "Core/UI/UIBase.h"
#include "Scene/Node.h"

namespace ya
{

struct Node2D;

/// Walks the unified scene tree, laying out and painting Node2D (UI) nodes,
/// and dispatches events / picking for them. Screen-space top-left origin, Y
/// down — the same convention as Render2D and UIAppCtx.
struct UISceneRenderer
{
    /// Layout + paint every visible Node2D in the tree into the active Render2D
    /// batch (must be called inside Render2D::begin/end). `uiScale` maps
    /// logical viewport pixels to render-target pixels;
    /// `logicalViewportExtent` is the canvas root size in logical pixels.
    static void render(Node* sceneRoot,
                       const glm::vec2& uiScale,
                       const UICanvasTransform& canvas,
                       const Extent2D& logicalViewportExtent);

    /// Hit-test the Node2D subtree (topmost-first, same order as paint) and
    /// dispatch mouse events. Returns true when the UI consumed the event
    /// (caller should not pass it to gameplay).
    static bool handleEvent(const Event& event, const UIAppCtx& ctx, Node* sceneRoot);

    /// Topmost-first pick of any visible Node2D under `canvasPoint` (canvas
    /// logical space). Shared with the editor 2D canvas picking.
    static Node2D* pickNodeAt(Node* sceneRoot, const glm::vec2& canvasPoint);
};

} // namespace ya
