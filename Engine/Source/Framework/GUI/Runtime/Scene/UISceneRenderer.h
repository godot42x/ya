#pragma once

#include "Foundation/Core/Event.h"
#include "Framework/GUI/Runtime/UIBase.h"
#include "Framework/GUI/Runtime/Scene/Node.h"

namespace ya
{

struct Node2D;

/// Result of one game-UI event route pass.
enum class EUIRouteResult : uint8_t
{
    NotHandled,       // no UI node consumed the event
    HandledPass,      // UI responded but the event also falls through to the game
    HandledExclusive, // a Stop node consumed the event; the game must not receive it
};

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
    /// dispatch mouse events. Honours each node's `_hitFilter`: Stop hits
    /// consume exclusively, Pass hits respond but fall through, Ignore nodes
    /// are skipped.
    static EUIRouteResult handleEvent(const Event& event, const UIAppCtx& ctx, Node* sceneRoot);

    /// Topmost-first pick of any visible Node2D under `canvasPoint` (canvas
    /// logical space). Shared with the editor 2D canvas picking.
    static Node2D* pickNodeAt(Node* sceneRoot, const glm::vec2& canvasPoint);
};

} // namespace ya
