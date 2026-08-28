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
    // A compound is a local composition root. Its first child is the
    // composition host and receives the same assigned rect; the host then
    // arranges its own descendants using the normal layout contract.
    const auto& children = getChildren();
    if (!children.empty() && children.front()) {
        children.front()->layoutAssigned(_layoutRect);
    }
}

glm::vec2 UICompoundWidget::computeDesiredSize() const
{
    const auto& children = getChildren();
    return children.empty() || !children.front() ? _size : children.front()->computeDesiredSize();
}

} // namespace ya
