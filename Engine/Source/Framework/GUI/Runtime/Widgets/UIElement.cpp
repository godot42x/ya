#include "GUI/Widgets/UIElement.h"

#include "Core/Log.h"

#include <algorithm>

namespace ya
{

UIElement::UIElement(std::string name) : _name(std::move(name)) {}

UIElement::~UIElement()
{
    // A widget must never be destroyed while it still belongs to a live tree
    // (the tree would later walk freed memory). WidgetTree detaches members
    // on tree destruction; direct destruction while attached is a bug.
    YA_CORE_ASSERT(_tree == nullptr, "UIElement destroyed while still attached to a WidgetTree");

    // The visual parent/tree holds children strongly, so a parent normally
    // outlives its children. If this widget dies first (detached subtree
    // where the business drops the root ref), sever the children's back-links
    // so they never dangle into the destroyed parent.
    for (const auto& child : _children) {
        child->_tree   = nullptr;
        child->_parent = nullptr;
    }
}

std::vector<UIElement*> UIElement::getChildrenInPaintOrder() const
{
    std::vector<UIElement*> children;
    children.reserve(_children.size());
    for (const auto& child : _children) {
        children.push_back(child.get());
    }
    std::stable_sort(children.begin(), children.end(), [](const UIElement* a, const UIElement* b) {
        return a->_zOrder < b->_zOrder;
    });
    return children;
}

// === Effective-state queries ===

bool UIElement::isVisibleInTree() const
{
    for (const UIElement* node = this; node != nullptr; node = node->_parent) {
        if (!node->isVisibleForRender()) {
            return false;
        }
    }
    return true;
}

bool UIElement::isHitTestableInTree() const
{
    if (!isHitTestableSubtree()) {
        return false;
    }
    for (const UIElement* node = _parent; node != nullptr; node = node->_parent) {
        // Hidden / Collapsed cull rendering and hits; SelfHitTestInvisible
        // culls hits only. HitTestInvisible ancestors do not block children.
        if (!node->isVisibleForRender() || !node->isHitTestableSubtree()) {
            return false;
        }
    }
    return true;
}

bool UIElement::hitTestLayoutRect(const glm::vec2& logicalPoint) const
{
    return logicalPoint.x >= _layoutRect.pos.x &&
           logicalPoint.x <= _layoutRect.pos.x + _layoutRect.extent.x &&
           logicalPoint.y >= _layoutRect.pos.y &&
           logicalPoint.y <= _layoutRect.pos.y + _layoutRect.extent.y;
}

// === Layout ===

Rect2D UIElement::computeAnchorRect(const Rect2D& parentRect) const
{
    const glm::vec2 anchorMin = glm::clamp(_anchorMin, 0.0f, 1.0f);
    const glm::vec2 anchorMax = glm::clamp(_anchorMax, 0.0f, 1.0f);
    const glm::vec2 rectMin   = parentRect.pos + parentRect.extent * anchorMin + _position;

    // Per-axis: an axis with an anchor span stretches to the parent; otherwise
    // the axis keeps _size (default {0,0} anchors = legacy absolute layout).
    const glm::vec2 span = (anchorMax - anchorMin) * parentRect.extent;
    glm::vec2       size = _size;
    if (span.x != 0.0f) {
        size.x = span.x;
    }
    if (span.y != 0.0f) {
        size.y = span.y;
    }
    return Rect2D{.pos = rectMin, .extent = size};
}

void UIElement::layout(const Rect2D& parentRect)
{
    _layoutRect = computeAnchorRect(parentRect);
    layoutChildren(_layoutRect);
}

void UIElement::layoutAssigned(const Rect2D& rect)
{
    _layoutRect = rect;
    layoutChildren(_layoutRect);
}

void UIElement::layoutChildren(const Rect2D& layoutRect)
{
    for (UIElement* child : getChildrenInPaintOrder()) {
        if (child->participatesInLayout()) {
            child->layout(layoutRect);
        }
    }
}

glm::vec2 UIElement::computeDesiredSize() const
{
    return _size;
}

// === Paint ===

void UIElement::paint(const WidgetPaintContext& ctx)
{
    if (!isVisibleForRender()) {
        return;
    }
    paintSelf(ctx);
    paintChildren(ctx);
}

void UIElement::paintChildren(const WidgetPaintContext& ctx)
{
    for (UIElement* child : getChildrenInPaintOrder()) {
        child->paint(ctx);
    }
}

// === Events ===

bool UIElement::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    (void)event;
    (void)ctx;
    return false; // Passive: base/panels/text never consume events.
}

} // namespace ya
