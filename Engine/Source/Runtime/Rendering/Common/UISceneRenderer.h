#pragma once

#include "Core/Event.h"
#include "Core/UI/UIBase.h"
#include "Scene/Node.h"

namespace ya
{

/// Walks the unified scene tree, collecting Node2D (UI) nodes, and renders /
/// dispatches events for them. Screen-space top-left origin, Y down — the same
/// convention as Render2D and UIAppCtx.
struct UISceneRenderer
{
    /// Render every visible Node2D in the tree into the active Render2D batch
    /// (must be called inside Render2D::begin/end), sorted by zOrder.
    /// `uiScale` maps logical viewport pixels to render-target pixels (frame
    /// buffer scale).
    static void render(Node* sceneRoot, const glm::vec2& uiScale = glm::vec2(1.0f));

    /// Hit-test the Node2D subtree and dispatch mouse events. Returns true when
    /// the UI consumed the event (caller should not pass it to gameplay).
    static bool handleEvent(const Event& event, const UIAppCtx& ctx, Node* sceneRoot);
};

} // namespace ya
