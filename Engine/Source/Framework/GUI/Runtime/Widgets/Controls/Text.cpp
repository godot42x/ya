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
    if (_bWrap) {
        // Wrapped text paints line by line; each line is its own text item
        // so the incremental cache stays granular.
        const float maxWidth = _maxWrapWidth > 0.0f ? _maxWrapWidth : _layoutRect.extent.x;
        const auto lines     = wrapText(text, font, maxWidth);
        float      lineY     = _layoutRect.pos.y;
        for (const std::string& line : lines) {
            builder.addText(Rect2D{.pos = {_layoutRect.pos.x, lineY},
                                   .extent = {_layoutRect.extent.x, font->lineHeight}},
                            line, style.textColor, font, _hAlign, EWidgetAlignV::Top);
            lineY += font->lineHeight;
        }
        return;
    }
    builder.addText(_layoutRect, text, style.textColor, font, _hAlign, _vAlign);
}

std::vector<std::string> UIText::wrapText(const std::string& text,
                                          const std::shared_ptr<Font>& font,
                                          float maxWidth)
{
    std::vector<std::string> lines;
    if (!font || maxWidth <= 0.0f) {
        lines.push_back(text);
        return lines;
    }
    std::string current;
    size_t      i = 0;
    while (i < text.size()) {
        // Take one UTF-8 code point at a time (CJK-safe).
        size_t next = i + 1;
        while (next < text.size() && (static_cast<unsigned char>(text[next]) & 0xC0) == 0x80) {
            ++next;
        }
        const std::string candidate = current + text.substr(i, next - i);
        if (!current.empty() && font->measureText(candidate) > maxWidth) {
            lines.push_back(current);
            current.clear();
        }
        current += text.substr(i, next - i);
        i = next;
    }
    if (!current.empty() || lines.empty()) {
        lines.push_back(current);
    }
    return lines;
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
    if (_bWrap) {
        const float maxWidth = _maxWrapWidth > 0.0f ? _maxWrapWidth : _size.x;
        const auto  lines    = wrapText(resolvedText(ReactiveBase::EDirtyLevel::Layout), font, maxWidth);
        return {maxWidth, static_cast<float>(lines.size()) * font->lineHeight};
    }
    return {font->measureText(resolvedText(ReactiveBase::EDirtyLevel::Layout)), font->lineHeight};
}

} // namespace ya
