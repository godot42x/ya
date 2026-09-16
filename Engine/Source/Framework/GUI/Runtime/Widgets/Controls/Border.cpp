#include "GUI/Widgets/Controls/Border.h"

#include "GUI/Widgets/UIFrameSnapshot.h"

namespace ya
{

UIBorder::UIBorder(std::string name)
    : UIElement(std::move(name), "panel")
{
    bindHostLayout(_contentLayout);
}

void UIBorder::setColor(const glm::vec4& value)
{
    if (_color == value) {
        return;
    }
    _color = value;
    setStyleField("fillColor", FBrush::solid(value), EUIPropertyImpact::Paint);
}

void UIBorder::setCornerRadius(float value)
{
    if (_cornerRadius == value) {
        return;
    }
    _cornerRadius = value;
    invalidateProperty(EUIPropertyImpact::Paint);
}

void UIBorder::paintSelf(UIFrameBuilder& builder)
{
    const FPanelStyle& style = resolvedStyle();
    // The panel surface (fill + radius + edge) comes from the style brush;
    // `_cornerRadius` is the per-instance override the designer/document path
    // uses on top of it (instanceEditable), so one card can be rounder without
    // the theme growing a key per radius.
    FBrush fill = style.fillColor;
    if (_cornerRadius > 0.0f) {
        fill.cornerRadius = _cornerRadius;
    }
    builder.addBrush(_layoutRect, fill);
}

const FPanelStyle& UIBorder::resolvedStyle(ReactiveBase::EDirtyLevel level, bool bTrackDependencies) const
{
    return resolvedStyleCache(*this, level,
                              [this](FPanelStyle& style) {
                                  const bool bThemed = !_styleKey.empty() && getTree() && getTree()->getTheme()
                                                       && getTree()->getTheme()->find<FPanelStyle>(_styleKey);
                                  if (bThemed) {
                                      return;
                                  }
                                  const bool bHasFill = _authoredStyle.is_object() && _authoredStyle.contains("fillColor");
                                  if (!bHasFill) {
                                      style.fillColor = FBrush::solid(_color);
                                  }
                              },
                              bTrackDependencies);
}

glm::vec2 UIBorder::computeDesiredSize() const
{
    return _contentLayout.measure(*this);
}

std::unique_ptr<UISlot> UIBorder::createSlotForChild(UIElement& child)
{
    return _contentLayout.createSlot(*this, child);
}

} // namespace ya
