#include "GUI/Widgets/Controls/ScrollViewport.h"

#include "Core/Log.h"
#include "GUI/Widgets/UIFrameSnapshot.h"

namespace ya
{

void UIScrollViewport::layout(const Rect2D& parentRect)
{
    layoutAssigned(computeAnchorRect(parentRect));
}

void UIScrollViewport::layoutAssigned(const Rect2D& rect)
{
    _layoutRect = rect;
    _layoutRect.extent = glm::max(_layoutRect.extent, glm::vec2(0.0f));
    if (getChildren().size() > 1) {
        YA_CORE_WARN("UIScrollViewport '{}': UIScrollLayout only scrolls the first child ({} attached)",
                     _name, getChildren().size());
    }
    _scrollLayout.arrange(*this, _layoutRect);
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
