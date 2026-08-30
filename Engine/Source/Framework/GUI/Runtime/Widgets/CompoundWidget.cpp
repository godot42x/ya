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

void UICompoundWidget::layout(const Rect2D& parentRect)
{
    layoutAssigned(computeAnchorRect(parentRect));
}

void UICompoundWidget::layoutAssigned(const Rect2D& rect)
{
    setLayoutRect(rect);
    _contentLayout.arrange(*this, _layoutRect);
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
