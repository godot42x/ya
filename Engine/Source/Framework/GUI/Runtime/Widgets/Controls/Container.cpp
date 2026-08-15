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

void UIContainer::paint(UIFrameBuilder& builder)
{
    if (!isVisibleForRender()) {
        return;
    }
    builder.countWidget();
    pushPaintWidget(this);
    // Containers always re-run paintSelf (cheap): they establish the clip
    // context for their children, whose paint() decides reuse vs re-run.
    paintSelf(builder);
    if (_boxLayout.clipsChildren()) {
        builder.pushClip(_layoutRect);
    }
    paintChildren(builder);
    if (_boxLayout.clipsChildren()) {
        builder.popClip();
    }
    popPaintWidget();
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
