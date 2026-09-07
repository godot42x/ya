#include "GUI/Widgets/Controls/Overlay.h"

namespace ya
{

UIOverlay::UIOverlay(std::string name)
    : UIElement(std::move(name))
{
    bindHostLayout(_overlayLayout);
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
