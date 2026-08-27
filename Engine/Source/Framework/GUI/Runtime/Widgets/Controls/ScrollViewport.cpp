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

void UIScrollViewport::layout(const Rect2D& parentRect)
{
    layoutAssigned(computeAnchorRect(parentRect));
}

void UIScrollViewport::layoutAssigned(const Rect2D& rect)
{
    setLayoutRect(rect);
    if (getChildren().size() > 1) {
        YA_CORE_WARN("UIScrollViewport '{}': UIScrollLayout only scrolls the first child ({} attached)",
                     _name, getChildren().size());
    }
    _scrollLayout.arrange(*this, _layoutRect);
}

void UIScrollViewport::paintSelf(UIFrameBuilder& builder)
{
    // Scrollbar: the thumb position is a paint attribute derived from the
    // scroll offset, so every offset change must re-paint this widget (the
    // wheel handler marks paint-dirty on a real scroll).
    if (!_bShowScrollbar || !isScrollable()) {
        return;
    }
    // Theme resolution (style-system Phase 3): track/thumb brushes + width
    // from FScrollBarStyle. Paint-only (the width does not inset content).
    // Absent key/theme → default-constructed style is the fallback (Phase 3
    // cleanup: no bare fields).
    const FScrollBarStyle style = resolveWidgetStyle<FScrollBarStyle>(*this, _authoredStyle);

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

void UIScrollViewport::paintChildren(UIFrameBuilder& builder)
{
    builder.pushClip(_layoutRect);
    UIElement::paintChildren(builder);
    builder.popClip();
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

} // namespace ya
