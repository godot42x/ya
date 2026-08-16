#include "GUI/Widgets/Controls/Container.h"

#include "GUI/Widgets/Reactive.h"
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
    setLayoutRect(rect);
    _boxLayout.arrange(*this, _layoutRect);
}

void UIContainer::paintChildren(UIFrameBuilder& builder)
{
    if (_boxLayout.clipsChildren()) {
        builder.pushClip(_layoutRect);
    }
    UIElement::paintChildren(builder);
    if (_boxLayout.clipsChildren()) {
        builder.popClip();
    }
}

void UIContainer::onLayoutRectChanged()
{
    // Only a clipping container owns an inherited clip: its rect is the clip
    // rect for every descendant, so a rect change must invalidate the whole
    // subtree's resolved segments (a fixed-size child keeps its own rect but
    // its cached item still holds the old clip).
    if (_boxLayout.clipsChildren()) {
        invalidateSubtree(EUIInvalidationReason::InheritedPaintContext);
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
