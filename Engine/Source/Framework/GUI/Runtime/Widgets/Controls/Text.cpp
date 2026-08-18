#include "GUI/Widgets/Controls/Text.h"

#include "Render/Resources/FontManager.h"
#include "GUI/Widgets/UIFrameSnapshot.h"

namespace ya
{

void UIText::paintSelf(UIFrameBuilder& builder)
{
    // Resolve the (possibly reactive) text first so the dependency is recorded
    // even when no font is available and the item is skipped.
    //
    // AutoSize text: a text/fontSize change alters the desired size, so those
    // reads are Layout edges (a write must re-run measure+arrange). Fixed-size
    // text only repaints: Paint edges.
    const ReactiveBase::EDirtyLevel level = _bAutoSize ? ReactiveBase::EDirtyLevel::Layout
                                                       : ReactiveBase::EDirtyLevel::Paint;
    const std::string&               text  = resolvedText(level);
    const FWidgetStyle               style = resolvedStyle(level);
    auto                             font  = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, style.fontSize);
    if (!font) {
        return;
    }
    if (_bFillBackground) {
        // Background = layout rect expanded by the style padding, so a themed
        // badge/chip reads as a padded block rather than a tight underline.
        Rect2D bg = _layoutRect;
        bg.pos -= style.padding;
        bg.extent += style.padding * 2.0f;
        builder.addSprite(bg, style.fillColor, nullptr);
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

FWidgetStyle UIText::resolvedStyle(ReactiveBase::EDirtyLevel level) const
{
    FWidgetStyle style;
    style.fillColor = _color;
    style.textColor = _color;
    style.fontSize  = _fontSize;
    if (_styleBinding) {
        const FWidgetStyle& bound = _styleBinding->get(level); // records the dependency
        style.fillColor          = bound.fillColor;
        style.textColor          = bound.textColor;
        style.fontSize           = bound.fontSize;
        style.padding            = bound.padding;
    }
    return style;
}

glm::vec2 UIText::computeDesiredSize() const
{
    if (!_bAutoSize) {
        return _size;
    }
    // Measure from the resolved text/style so the desired size matches paint
    // exactly when a binding is active. (Measure runs during layout, before
    // the paint walk, so get() here does not register a dependency; the Layout
    // edge is instead established by paintSelf at the same level.)
    const FWidgetStyle style = resolvedStyle(ReactiveBase::EDirtyLevel::Layout);
    auto               font  = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, style.fontSize);
    if (!font) {
        return _size;
    }
    return {font->measureText(resolvedText(ReactiveBase::EDirtyLevel::Layout)), font->lineHeight};
}

} // namespace ya
