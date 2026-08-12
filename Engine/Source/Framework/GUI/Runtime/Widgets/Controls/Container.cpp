#include "GUI/Widgets/Controls/Container.h"

#include "GUI/Widgets/UIFrameSnapshot.h"

#include <algorithm>

namespace ya
{

void UIContainer::layout(const Rect2D& parentRect)
{
    layoutAssigned(computeAnchorRect(parentRect));
}

void UIContainer::layoutAssigned(const Rect2D& rect)
{
    _layoutRect = rect;
    Rect2D      content = _layoutRect;
    content.pos += _padding;
    content.extent -= _padding * 2.0f;
    arrangeChildren(content);
}

void UIContainer::arrangeChildren(const Rect2D& contentRect)
{
    const bool bHorizontal = _direction == EWidgetBoxLayout::Horizontal;

    // Packed extent of all visible children + spacing along the main axis.
    float  packedExtent = 0.0f;
    size_t visibleCount = 0;
    for (UIElement* child : getChildrenInPaintOrder()) {
        if (!child->participatesInLayout()) {
            continue;
        }
        const glm::vec2 desired = child->computeDesiredSize();
        packedExtent += bHorizontal ? desired.x : desired.y;
        ++visibleCount;
    }
    if (visibleCount > 0) {
        packedExtent += static_cast<float>(visibleCount - 1) * _spacing;
    }

    // Main-axis start cursor honoring the alignment.
    const float contentStart  = bHorizontal ? contentRect.pos.x : contentRect.pos.y;
    const float contentExtent = bHorizontal ? contentRect.extent.x : contentRect.extent.y;
    float       cursor        = contentStart;
    switch (_mainAxisAlignment) {
    case EWidgetMainAxisAlignment::Center:
        cursor = contentStart + std::max(0.0f, (contentExtent - packedExtent) * 0.5f);
        break;
    case EWidgetMainAxisAlignment::End:
        cursor = contentStart + std::max(0.0f, contentExtent - packedExtent);
        break;
    case EWidgetMainAxisAlignment::Start:
        break;
    }

    for (UIElement* child : getChildrenInPaintOrder()) {
        if (!child->participatesInLayout()) {
            continue;
        }
        const glm::vec2 desired = child->computeDesiredSize();
        Rect2D          childRect;
        if (bHorizontal) {
            childRect = Rect2D{
                .pos    = {cursor, contentRect.pos.y},
                .extent = {desired.x, contentRect.extent.y},
            };
            cursor += desired.x + _spacing;
        }
        else {
            childRect = Rect2D{
                .pos    = {contentRect.pos.x, cursor},
                .extent = {contentRect.extent.x, desired.y},
            };
            cursor += desired.y + _spacing;
        }
        child->layoutAssigned(childRect);
    }
}

void UIContainer::paint(UIFrameBuilder& builder)
{
    if (!isVisibleForRender()) {
        return;
    }
    paintSelf(builder);
    if (_bClipChildren) {
        builder.pushClip(_layoutRect);
    }
    paintChildren(builder);
    if (_bClipChildren) {
        builder.popClip();
    }
}

glm::vec2 UIContainer::computeDesiredSize() const
{
    const bool bHorizontal = _direction == EWidgetBoxLayout::Horizontal;
    float      content     = 0.0f;
    float      cross       = 0.0f;
    size_t     count       = 0;
    for (const auto& child : getChildren()) {
        if (!child->participatesInLayout()) {
            continue;
        }
        const glm::vec2 desired = child->computeDesiredSize();
        if (bHorizontal) {
            content += desired.x;
            cross = std::max(cross, desired.y);
        }
        else {
            content += desired.y;
            cross = std::max(cross, desired.x);
        }
        ++count;
    }
    if (count > 0) {
        content += static_cast<float>(count - 1) * _spacing;
    }
    // Always return {width, height}: horizontal packs along x (content is
    // the width), vertical packs along y (content is the height, cross is
    // the width). Scroll viewports and split panes rely on the component
    // order matching the axis, regardless of direction.
    if (bHorizontal) {
        return {content + _padding.x * 2.0f, cross + _padding.y * 2.0f};
    }
    return {cross + _padding.x * 2.0f, content + _padding.y * 2.0f};
}

} // namespace ya
