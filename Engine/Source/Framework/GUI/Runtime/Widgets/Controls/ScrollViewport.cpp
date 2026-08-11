#include "GUI/Widgets/Controls/ScrollViewport.h"

#include "Core/Log.h"

#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"

#include <algorithm>

namespace ya
{

void UIScrollViewport::layout(const Rect2D& parentRect)
{
    layoutAssigned(computeAnchorRect(parentRect));
}

void UIScrollViewport::layoutAssigned(const Rect2D& rect)
{
    _layoutRect = rect;
    const auto children = getChildrenInPaintOrder();
    if (children.size() > 1) {
        YA_CORE_WARN("UIScrollViewport '{}': only the first child is scrolled ({} attached)",
                     _name, children.size());
    }
    if (children.empty()) {
        _maxScrollOffset = 0.0f;
        return;
    }

    const bool  bVertical = _axis == EScrollAxis::Vertical;
    const glm::vec2 desired = children[0]->computeDesiredSize();

    // Scroll axis: at least the viewport size, up to the content desired
    // size. Cross axis: stretch to the viewport.
    const float contentMain = bVertical ? std::max(desired.y, _layoutRect.extent.y)
                                        : std::max(desired.x, _layoutRect.extent.x);
    const float viewportMain = bVertical ? _layoutRect.extent.y : _layoutRect.extent.x;
    _maxScrollOffset = std::max(0.0f, contentMain - viewportMain);

    // Clamp the offset at layout time (content size is the fact source).
    _scrollOffset = std::clamp(_scrollOffset, 0.0f, _maxScrollOffset);

    Rect2D contentRect = _layoutRect;
    if (bVertical) {
        contentRect.pos.y -= _scrollOffset;
        contentRect.extent = {_layoutRect.extent.x, contentMain};
    }
    else {
        contentRect.pos.x -= _scrollOffset;
        contentRect.extent = {contentMain, _layoutRect.extent.y};
    }
    children[0]->layoutAssigned(contentRect);
}

void UIScrollViewport::paint(UIFrameBuilder& builder)
{
    if (!isVisibleForRender()) {
        return;
    }
    paintSelf(builder);
    builder.pushClip(_layoutRect);
    paintChildren(builder);
    builder.popClip();
}

bool UIScrollViewport::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    if (event.getEventType() != EEvent::MouseScrolled) {
        return false;
    }

    // Innermost-first consumption: only scroll when there is room; otherwise
    // return false so the event bubbles to an outer scroll viewport.
    if (!isScrollable()) {
        return false;
    }

    const auto& scrolled = static_cast<const MouseScrolledEvent&>(event);
    const float delta    = _axis == EScrollAxis::Vertical ? -scrolled.getOffsetY() : -scrolled.getOffsetX();
    if (delta == 0.0f) {
        return false;
    }

    const float newOffset = std::clamp(_scrollOffset + delta * _scrollStep, 0.0f, _maxScrollOffset);
    if (newOffset == _scrollOffset) {
        return false; // already at the limit on this axis: bubble outward
    }
    _scrollOffset = newOffset;
    if (WidgetTree* tree = getTree()) {
        tree->invalidateLayout();
    }
    return true;
}

glm::vec2 UIScrollViewport::computeDesiredSize() const
{
    const auto children = getChildren();
    if (children.empty()) {
        return _size;
    }
    // The viewport is a window, not a grow-to-content box: its desired size
    // is its own configured size.
    return _size;
}

} // namespace ya

YA_REFLECT_ENUM_BEGIN(ya::EScrollAxis)
YA_REFLECT_ENUM_VALUE(Vertical)
YA_REFLECT_ENUM_VALUE(Horizontal)
YA_REFLECT_ENUM_END()
