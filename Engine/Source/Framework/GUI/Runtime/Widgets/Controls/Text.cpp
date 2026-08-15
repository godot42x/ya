#include "GUI/Widgets/Controls/Text.h"

#include "GUI/Resources/FontManager.h"
#include "GUI/Widgets/UIFrameSnapshot.h"

namespace ya
{

void UIText::paintSelf(UIFrameBuilder& builder)
{
    // Resolve the (possibly reactive) text first so the dependency is recorded
    // even when no font is available and the item is skipped.
    const std::string& text = resolvedText();
    const FWidgetStyle style = resolvedStyle();
    auto               font  = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, style.fontSize);
    if (!font) {
        return;
    }
    builder.addText(_layoutRect, text, style.textColor, font, _hAlign, _vAlign);
}

void UIText::bindStyle(std::shared_ptr<Reactive<FWidgetStyle>> style)
{
    // Paint attributes are collected during the paint walk (via get()), not
    // registered at bind time — a bind-time registration would be cleared by
    // the base paint's clearDependencies() on the next re-run.
    _styleBinding = std::move(style);
}

FWidgetStyle UIText::resolvedStyle() const
{
    FWidgetStyle style;
    style.textColor = _color;
    style.fontSize  = _fontSize;
    if (_styleBinding) {
        const FWidgetStyle& bound = _styleBinding->get(); // records the dependency
        style.textColor          = bound.textColor;
        style.fontSize           = bound.fontSize;
    }
    return style;
}

glm::vec2 UIText::computeDesiredSize() const
{
    if (!_bAutoSize) {
        return _size;
    }
    auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, _fontSize);
    if (!font) {
        return _size;
    }
    return {font->measureText(_text), font->lineHeight};
}

} // namespace ya
