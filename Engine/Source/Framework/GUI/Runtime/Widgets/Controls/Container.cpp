#include "GUI/Widgets/Controls/Container.h"

#include "GUI/Draw2D/Render2D.h"

#include <algorithm>

namespace ya
{

void UIContainer::layout(const Rect2D& parentRect)
{
    _layoutRect = computeAnchorRect(parentRect);

    const float pad = _padding;
    Rect2D      content = _layoutRect;
    content.pos += glm::vec2(pad, pad);
    content.extent -= glm::vec2(pad * 2.0f, pad * 2.0f);
    arrangeChildren(content);
}

void UIContainer::arrangeChildren(const Rect2D& contentRect)
{
    const bool bHorizontal = _direction == EWidgetBoxLayout::Horizontal;
    float      cursor      = bHorizontal ? contentRect.pos.x : contentRect.pos.y;

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

void UIContainer::paint(const WidgetPaintContext& ctx)
{
    if (!isVisibleForRender()) {
        return;
    }
    paintSelf(ctx);
    if (_bClipChildren) {
        Render2D::pushClipRect(Rect2D{
            .pos    = _layoutRect.pos * ctx.uiScale,
            .extent = _layoutRect.extent * ctx.uiScale,
        });
    }
    paintChildren(ctx);
    if (_bClipChildren) {
        Render2D::popClipRect();
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
    return {content + _padding * 2.0f, cross + _padding * 2.0f};
}

} // namespace ya
