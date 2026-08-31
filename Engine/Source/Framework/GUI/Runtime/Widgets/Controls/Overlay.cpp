#include "GUI/Widgets/Controls/Overlay.h"

namespace ya
{

UIOverlay::UIOverlay(std::string name)
    : UIElement(std::move(name))
{
    _overlayLayout.setOwner(*this);
}

void UIOverlay::layout(const Rect2D& parentRect)
{
    layoutAssigned(parentRect);
}

void UIOverlay::layoutAssigned(const Rect2D& rect)
{
    setLayoutRect(rect);
    _overlayLayout.arrange(*this, _layoutRect);
}

glm::vec2 UIOverlay::computeDesiredSize() const
{
    return _overlayLayout.measure(*this);
}

std::unique_ptr<UISlot> UIOverlay::createSlotForChild(UIElement& child)
{
    return _overlayLayout.createSlot(*this, child);
}

} // namespace ya
