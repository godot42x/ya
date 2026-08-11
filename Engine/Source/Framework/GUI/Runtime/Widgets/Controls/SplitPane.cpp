#include "GUI/Widgets/Controls/SplitPane.h"

#include "Core/Log.h"

#include "GUI/Widgets/WidgetTree.h"

#include <algorithm>

namespace ya
{

namespace
{

float axisCoord(const glm::vec2& point, ESplitOrientation orientation)
{
    return orientation == ESplitOrientation::Vertical ? point.x : point.y;
}

float axisExtent(const Rect2D& rect, ESplitOrientation orientation)
{
    return orientation == ESplitOrientation::Vertical ? rect.extent.x : rect.extent.y;
}

bool pointInRect(const glm::vec2& point, const Rect2D& rect)
{
    return point.x >= rect.pos.x && point.x <= rect.pos.x + rect.extent.x &&
           point.y >= rect.pos.y && point.y <= rect.pos.y + rect.extent.y;
}

} // namespace

void UISplitPane::clampRatio(const Rect2D& contentRect)
{
    const float contentExtent = axisExtent(contentRect, _orientation);
    if (contentExtent <= 0.0f) {
        _splitRatio = 0.5f;
        return;
    }
    const float minRatio = std::clamp(_minFirstExtent / contentExtent, 0.0f, 1.0f);
    const float maxRatio = std::clamp(1.0f - _minSecondExtent / contentExtent, 0.0f, 1.0f);
    _splitRatio = std::clamp(_splitRatio, std::min(minRatio, maxRatio), std::max(minRatio, maxRatio));
}

void UISplitPane::layout(const Rect2D& parentRect)
{
    layoutAssigned(computeAnchorRect(parentRect));
}

void UISplitPane::layoutAssigned(const Rect2D& rect)
{
    _layoutRect = rect;
    const auto children = getChildrenInPaintOrder();
    if (children.size() > 2) {
        YA_CORE_WARN("UISplitPane '{}': only the first two children are laid out ({} attached)",
                     _name, children.size());
    }
    if (children.empty()) {
        return;
    }

    clampRatio(_layoutRect);

    const bool     bVertical = _orientation == ESplitOrientation::Vertical;
    const float    thickness = _dividerThickness;
    const float    contentExtent = axisExtent(_layoutRect, _orientation);
    const float    dividerCenter = axisCoord(_layoutRect.pos, _orientation) + contentExtent * _splitRatio;

    Rect2D firstRect  = _layoutRect;
    Rect2D secondRect = _layoutRect;
    if (bVertical) {
        firstRect.extent.x  = std::max(0.0f, dividerCenter - thickness * 0.5f - firstRect.pos.x);
        secondRect.pos.x    = dividerCenter + thickness * 0.5f;
        secondRect.extent.x = std::max(0.0f, _layoutRect.pos.x + _layoutRect.extent.x - secondRect.pos.x);
    }
    else {
        firstRect.extent.y  = std::max(0.0f, dividerCenter - thickness * 0.5f - firstRect.pos.y);
        secondRect.pos.y    = dividerCenter + thickness * 0.5f;
        secondRect.extent.y = std::max(0.0f, _layoutRect.pos.y + _layoutRect.extent.y - secondRect.pos.y);
    }

    children[0]->layoutAssigned(firstRect);
    if (children.size() >= 2) {
        children[1]->layoutAssigned(secondRect);
    }
}

Rect2D UISplitPane::getDividerRect() const
{
    const float thickness = _dividerThickness;
    const bool  bVertical = _orientation == ESplitOrientation::Vertical;
    const float contentExtent = axisExtent(_layoutRect, _orientation);
    const float dividerCenter = axisCoord(_layoutRect.pos, _orientation) + contentExtent * _splitRatio;

    Rect2D divider = _layoutRect;
    if (bVertical) {
        divider.pos.x    = dividerCenter - thickness * 0.5f;
        divider.extent.x = thickness;
    }
    else {
        divider.pos.y    = dividerCenter - thickness * 0.5f;
        divider.extent.y = thickness;
    }
    return divider;
}

bool UISplitPane::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    const EEvent::T eventType = event.getEventType();

    if (eventType == EEvent::MouseButtonPressed) {
        // Only a press on the divider starts a drag session; presses over a
        // pane fall through to the pane's own widgets.
        if (!pointInRect(ctx.logicalPoint, getDividerRect())) {
            return false;
        }
        _bDraggingDivider = true;
        _dragStartRatio   = _splitRatio;
        _dragStartPointer = axisCoord(ctx.logicalPoint, _orientation);
        if (WidgetTree* tree = getTree()) {
            tree->setFocus(this);
            tree->setPointerCapture(this);
        }
        return true;
    }

    // Pointer-captured events reach the split even outside its rect.
    if (!ctx.bViaCapture) {
        return false;
    }

    if (eventType == EEvent::MouseMoved && _bDraggingDivider) {
        const float  contentExtent = axisExtent(_layoutRect, _orientation);
        if (contentExtent > 0.0f) {
            _splitRatio = _dragStartRatio +
                          (axisCoord(ctx.logicalPoint, _orientation) - _dragStartPointer) / contentExtent;
            clampRatio(_layoutRect);
            if (WidgetTree* tree = getTree()) {
                tree->invalidateLayout();
            }
        }
        return true;
    }

    if (eventType == EEvent::MouseButtonReleased && _bDraggingDivider) {
        _bDraggingDivider = false;
        if (WidgetTree* tree = getTree()) {
            tree->releasePointerCapture(this);
        }
        return true;
    }

    return false;
}

void UISplitPane::clearTransientInputState()
{
    _bDraggingDivider = false;
}

glm::vec2 UISplitPane::computeDesiredSize() const
{
    const auto children = getChildren();
    if (children.empty()) {
        return _size;
    }

    const bool bVertical = _orientation == ESplitOrientation::Vertical;
    glm::vec2  desired{0.0f, 0.0f};
    for (const auto& child : children) {
        if (!child->participatesInLayout()) {
            continue;
        }
        const glm::vec2 childDesired = child->computeDesiredSize();
        if (bVertical) {
            desired.x += childDesired.x;
            desired.y = std::max(desired.y, childDesired.y);
        }
        else {
            desired.y += childDesired.y;
            desired.x = std::max(desired.x, childDesired.x);
        }
    }
    if (bVertical) {
        desired.x += _dividerThickness;
    }
    else {
        desired.y += _dividerThickness;
    }
    return desired;
}

} // namespace ya

YA_REFLECT_ENUM_BEGIN(ya::ESplitOrientation)
YA_REFLECT_ENUM_VALUE(Vertical)
YA_REFLECT_ENUM_VALUE(Horizontal)
YA_REFLECT_ENUM_END()
