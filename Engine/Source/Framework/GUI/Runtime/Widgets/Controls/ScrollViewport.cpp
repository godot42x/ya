#include "GUI/Widgets/Controls/ScrollViewport.h"

#include "Core/Log.h"
#include "GUI/Binding/Reactive.h"
#include "GUI/Widgets/UIFrameSnapshot.h"

#include <algorithm>

namespace ya
{

void UIScrollViewport::appendRuntimeDiagnostics(nlohmann::json& node, const WidgetTree&) const
{
    node["control"] = {
        {"type", "scrollViewport"},
        {"offset", getScrollOffset()},
        {"maxOffset", getMaxScrollOffset()},
    };
}

void UIScrollViewport::applyAssignedLayout(const Rect2D& rect)
{
    if (getChildren().size() > 1) {
        YA_CORE_WARN("UIScrollViewport '{}': UIScrollLayout only scrolls the first child ({} attached)",
                     _name, getChildren().size());
    }
    UIElement::applyAssignedLayout(rect);
}

bool UIScrollViewport::showsOverlayScrollbar() const
{
    return _bShowScrollbar && isScrollable();
}

Rect2D UIScrollViewport::contentClipRect() const
{
    Rect2D clip = _layoutRect;
    if (showsOverlayScrollbar()) {
        clip.extent.x = std::max(0.0f, clip.extent.x - resolvedStyle().width);
    }
    return clip;
}

void UIScrollViewport::paintScrollbarOverlay(UIFrameBuilder& builder) const
{
    if (!showsOverlayScrollbar()) {
        return;
    }
    const FScrollBarStyle& style = resolvedStyle();
    const float  trackX = _layoutRect.pos.x + _layoutRect.extent.x - style.width;
    const Rect2D track{
        .pos    = {trackX, _layoutRect.pos.y},
        .extent = {style.width, _layoutRect.extent.y},
    };
    builder.addBrush(track, style.trackColor);

    const float viewH    = _layoutRect.extent.y;
    const float contentH = viewH + getMaxScrollOffset();
    const float thumbH   = std::max(16.0f, viewH * viewH / contentH);
    const float thumbY   = _layoutRect.pos.y +
                           (viewH - thumbH) * (getScrollOffset() / getMaxScrollOffset());
    builder.addBrush(Rect2D{.pos = {trackX, thumbY}, .extent = {style.width, thumbH}}, style.thumbColor);
}

void UIScrollViewport::paintSelf(UIFrameBuilder&)
{
    // Overlay track/thumb is painted after children so full-width row fills
    // cannot cover the gutter. `FScrollBarStyle.width` stays paint-only.
}

void UIScrollViewport::paintChildren(UIFrameBuilder& builder)
{
    builder.pushClip(contentClipRect());
    UIElement::paintChildren(builder);
    builder.popClip();
    paintScrollbarOverlay(builder);
}

bool UIScrollViewport::cullsChildHits(const glm::vec2& logicalPoint) const
{
    if (!hitTestLayoutRect(logicalPoint)) {
        return true;
    }
    if (!showsOverlayScrollbar()) {
        return false;
    }
    const float gutterX = _layoutRect.pos.x + _layoutRect.extent.x - resolvedStyle().width;
    return logicalPoint.x >= gutterX;
}

void UIScrollViewport::onLayoutRectChanged()
{
    // The viewport rect is the content's clip rect: a changed viewport must
    // invalidate the content subtree's resolved segments even when the content
    // keeps its own layout rect.
    invalidateSubtree(EUIInvalidationReason::InheritedPaintContext);
}

bool UIScrollViewport::handleInputEvent(const Event& event, const WidgetEventContext&)
{
    if (event.getEventType() != EEvent::MouseScrolled) {
        return false;
    }
    const auto& scrolled = static_cast<const MouseScrolledEvent&>(event);
    const bool  bScrolled = _scrollLayout.scroll({scrolled.getOffsetX(), scrolled.getOffsetY()});
    if (bScrolled) {
        // The scrollbar thumb moved: this widget's own paint must re-run even
        // though its layout rect did not change.
        markPaintDirty();
    }
    return bScrolled;
}

glm::vec2 UIScrollViewport::computeDesiredSize() const
{
    return _scrollLayout.measure(*this);
}

std::unique_ptr<UISlot> UIScrollViewport::createSlotForChild(UIElement& child)
{
    return _scrollLayout.createSlot(*this, child);
}

} // namespace ya
