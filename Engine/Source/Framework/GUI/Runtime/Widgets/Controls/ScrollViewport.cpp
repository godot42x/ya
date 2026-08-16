#include "GUI/Widgets/Controls/ScrollViewport.h"

#include "Core/Log.h"
#include "GUI/Widgets/Reactive.h"
#include "GUI/Widgets/UIFrameSnapshot.h"

namespace ya
{

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
    return _scrollLayout.scroll({scrolled.getOffsetX(), scrolled.getOffsetY()});
}

glm::vec2 UIScrollViewport::computeDesiredSize() const
{
    return _scrollLayout.measure(*this);
}

} // namespace ya
