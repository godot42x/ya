#include "UI/UISceneRenderer.h"

#include "UI/Scene/Node2D.h"

#include <algorithm>

namespace ya
{

namespace
{

constexpr float kCanvasMinSize = 1.0f;

/// Children in paint order at any tree level: stable sort by zOrder ascending
/// (non-2D children key 0). Matches Node2D::getChildrenInPaintOrder.
std::vector<Node*> childrenInPaintOrder(Node* node)
{
    std::vector<Node*> children = node->getChildren();
    std::stable_sort(children.begin(), children.end(), [](const Node* a, const Node* b) {
        const int zA = a->is2D() ? static_cast<const Node2D*>(a)->_zOrder : 0;
        const int zB = b->is2D() ? static_cast<const Node2D*>(b)->_zOrder : 0;
        return zA < zB;
    });
    return children;
}

/// Layout + paint a 2D root against the canvas rect; non-2D branches are
/// traversed with the same rect (Node3D ancestors contribute no 2D transform).
void renderSubtree(Node* node, const Rect2D& canvasRect, const UIPaintContext& ctx)
{
    for (Node* child : childrenInPaintOrder(node)) {
        if (!child) {
            continue;
        }
        if (child->is2D()) {
            auto* node2D = static_cast<Node2D*>(child);
            node2D->layout(canvasRect);
            node2D->paint(ctx);
        }
        else {
            renderSubtree(child, canvasRect, ctx);
        }
    }
}

/// Clear transient input state on every Node2D (e.g. button hover).
void resetHoverSubtree(Node* node)
{
    if (!node) {
        return;
    }
    if (node->is2D()) {
        static_cast<Node2D*>(node)->resetHoverState();
    }
    for (Node* child : node->getChildren()) {
        resetHoverSubtree(child);
    }
}

/// Topmost-first hit test: children (zOrder descending) before self. Ignore
/// nodes skip their own hit test but still pass the event to their children;
/// Stop hits short-circuit with HandledExclusive; Pass hits accumulate a
/// HandledPass result while the walk continues.
EUIRouteResult hitTestSubtree(Node* node, const Event& event, const UIEventContext& ctx)
{
    if (!node) {
        return EUIRouteResult::NotHandled;
    }
    if (node->is2D()) {
        auto* node2D = static_cast<Node2D*>(node);
        if (!node2D->isHitTestableSubtree()) {
            return EUIRouteResult::NotHandled; // Hidden/Collapsed/SelfHitTestInvisible cull the subtree.
        }

        EUIRouteResult result = EUIRouteResult::NotHandled;
        const auto children = childrenInPaintOrder(node);
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
            const EUIRouteResult childResult = hitTestSubtree(*it, event, ctx);
            if (childResult == EUIRouteResult::HandledExclusive) {
                return EUIRouteResult::HandledExclusive;
            }
            if (childResult == EUIRouteResult::HandledPass) {
                result = EUIRouteResult::HandledPass;
            }
        }

        if (!node2D->isHitTestableSelf() ||
            !node2D->hitTestLayoutRect(ctx.canvasPoint) ||
            !node2D->handleInputEvent(event, ctx)) {
            return result;
        }
        return node2D->_hitFilter == EUIHitFilter::Stop ? EUIRouteResult::HandledExclusive
                                                        : EUIRouteResult::HandledPass;
    }

    EUIRouteResult result = EUIRouteResult::NotHandled;
    const auto children = childrenInPaintOrder(node);
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        const EUIRouteResult childResult = hitTestSubtree(*it, event, ctx);
        if (childResult == EUIRouteResult::HandledExclusive) {
            return EUIRouteResult::HandledExclusive;
        }
        if (childResult == EUIRouteResult::HandledPass) {
            result = EUIRouteResult::HandledPass;
        }
    }
    return result;
}

/// Topmost-first pick of any visible Node2D under the canvas point.
Node2D* pickSubtree(Node* node, const UIEventContext& ctx)
{
    if (!node) {
        return nullptr;
    }
    if (node->is2D()) {
        auto* node2D = static_cast<Node2D*>(node);
        if (!node2D->isHitTestableSubtree()) {
            return nullptr;
        }
        const bool bSelfSkipped = !node2D->isHitTestableSelf();
        const auto children = childrenInPaintOrder(node);
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
            if (Node2D* hit = pickSubtree(*it, ctx)) {
                return hit;
            }
        }
        if (!bSelfSkipped && node2D->hitTestLayoutRect(ctx.canvasPoint)) {
            return node2D;
        }
        return nullptr;
    }
    const auto children = childrenInPaintOrder(node);
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        if (Node2D* hit = pickSubtree(*it, ctx)) {
            return hit;
        }
    }
    return nullptr;
}

} // namespace

void UISceneRenderer::render(Node* sceneRoot,
                             const glm::vec2& uiScale,
                             const UICanvasTransform& canvas,
                             const Extent2D& logicalViewportExtent)
{
    if (!sceneRoot) {
        return;
    }

    const Rect2D canvasRect{
        .pos = {0.0f, 0.0f},
        .extent = {std::max(static_cast<float>(logicalViewportExtent.width), kCanvasMinSize),
                   std::max(static_cast<float>(logicalViewportExtent.height), kCanvasMinSize)},
    };
    const UIPaintContext ctx{
        .uiScale = uiScale,
        .canvas  = canvas,
    };
    renderSubtree(sceneRoot, canvasRect, ctx);
}

EUIRouteResult UISceneRenderer::handleEvent(const Event& event, const UIAppCtx& ctx, Node* sceneRoot)
{
    if (!sceneRoot || !ctx.bInViewport) {
        return EUIRouteResult::NotHandled;
    }

    const EEvent::T eventType = event.getEventType();
    if (eventType != EEvent::MouseButtonPressed &&
        eventType != EEvent::MouseButtonReleased &&
        eventType != EEvent::MouseMoved) {
        return EUIRouteResult::NotHandled;
    }

    // Viewport-local point in canvas logical space (uiScale conversion is a
    // known follow-up, see game-ui-rendering plan §5.4).
    const glm::vec2 point = ctx.lastMousePos - ctx.viewportRect.pos;

    if (eventType == EEvent::MouseMoved) {
        resetHoverSubtree(sceneRoot);
    }

    const UIEventContext uiCtx{
        .canvasPoint = point,
    };
    return hitTestSubtree(sceneRoot, event, uiCtx);
}

Node2D* UISceneRenderer::pickNodeAt(Node* sceneRoot, const glm::vec2& canvasPoint)
{
    if (!sceneRoot) {
        return nullptr;
    }
    const UIEventContext ctx{
        .canvasPoint = canvasPoint,
    };
    return pickSubtree(sceneRoot, ctx);
}

} // namespace ya
