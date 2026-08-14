#include "GUI/Widgets/Controls/Container.h"

#include "GUI/Widgets/UIFrameSnapshot.h"

namespace ya
{

UIContainer::UIContainer(std::string name)
    : UIElement(std::move(name))
{
    _boxLayout.setOwner(*this);
}

void UIContainer::layout(const Rect2D& parentRect)
{
    layoutAssigned(computeAnchorRect(parentRect));
}

void UIContainer::layoutAssigned(const Rect2D& rect)
{
    _layoutRect = rect;
    _layoutRect.extent = glm::max(_layoutRect.extent, glm::vec2(0.0f));
    _boxLayout.arrange(*this, _layoutRect);
}

void UIContainer::paint(UIFrameBuilder& builder)
{
    if (!isVisibleForRender()) {
        return;
    }
    paintSelf(builder);
    if (_boxLayout.clipsChildren()) {
        builder.pushClip(_layoutRect);
    }
    paintChildren(builder);
    if (_boxLayout.clipsChildren()) {
        builder.popClip();
    }
}

glm::vec2 UIContainer::computeDesiredSize() const
{
    return _boxLayout.measure(*this);
}

std::unique_ptr<UISlot> UIContainer::createSlotForChild(UIElement& child)
{
    return _boxLayout.createSlot(*this, child);
}

} // namespace ya
