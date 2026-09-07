#include "GUI/Widgets/CompoundWidget.h"

namespace ya
{

void UICompoundWidget::prepareForAttach()
{
    if (_bConstructed) {
        return;
    }
    construct();
    _bConstructed = true;
}

glm::vec2 UICompoundWidget::computeDesiredSize() const
{
    const auto& children = getChildren();
    if (children.empty() || !children.front()) {
        return computeIntrinsicSize();
    }
    return _contentLayout.measure(*this);
}

std::unique_ptr<UISlot> UICompoundWidget::createSlotForChild(UIElement& child)
{
    return _contentLayout.createSlot(*this, child);
}

} // namespace ya
