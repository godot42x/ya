#include "GUI/Widgets/Controls/SizeBox.h"

namespace ya
{

UISizeBox::UISizeBox(std::string name)
    : UIElement(std::move(name))
{
    bindHostLayout(_contentLayout);
}

void UISizeBox::setWidthOverride(float value)
{
    if (_widthOverride != value) {
        _widthOverride = value;
        invalidateProperty(EUIPropertyImpact::Layout);
    }
}

void UISizeBox::setHeightOverride(float value)
{
    if (_heightOverride != value) {
        _heightOverride = value;
        invalidateProperty(EUIPropertyImpact::Layout);
    }
}

void UISizeBox::setMinSize(glm::vec2 value)
{
    value = glm::max(value, glm::vec2(0.0f));
    if (_minSize != value) {
        _minSize = value;
        _maxSize = glm::max(_maxSize, _minSize);
        invalidateProperty(EUIPropertyImpact::Layout);
    }
}

void UISizeBox::setMaxSize(glm::vec2 value)
{
    value = glm::max(value, _minSize);
    if (_maxSize != value) {
        _maxSize = value;
        invalidateProperty(EUIPropertyImpact::Layout);
    }
}

glm::vec2 UISizeBox::computeDesiredSize() const
{
    glm::vec2 desired = _contentLayout.measure(*this);
    if (_widthOverride >= 0.0f) {
        desired.x = _widthOverride;
    }
    if (_heightOverride >= 0.0f) {
        desired.y = _heightOverride;
    }
    return glm::clamp(desired, _minSize, _maxSize);
}

std::unique_ptr<UISlot> UISizeBox::createSlotForChild(UIElement& child)
{
    return _contentLayout.createSlot(*this, child);
}

} // namespace ya
