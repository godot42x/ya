#include "GUI/Widgets/Controls/Text.h"

#include "Render/Resources/FontManager.h"
#include "GUI/Widgets/UIFrameSnapshot.h"

#include <nlohmann/json.hpp>

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
    const ReactiveBase::EDirtyLevel level = isAutoSizeActive() ? ReactiveBase::EDirtyLevel::Layout
                                                               : ReactiveBase::EDirtyLevel::Paint;
    const std::string&               text  = resolvedText(level);
    const FTextStyle                style = resolvedStyle(level);
    auto                             font  = resolveTextFont(style, builder.fontDpi());
    if (!font) {
        return;
    }
    if (_bFillBackground) {
        // Background = layout rect expanded by the style padding, so a themed
        // badge/chip reads as a padded block rather than a tight underline.
        Rect2D bg = _layoutRect;
        bg.pos -= style.padding;
        bg.extent += style.padding * 2.0f;
        builder.addBrush(bg, style.fillColor);
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

void UIText::appendRuntimeDiagnostics(nlohmann::json& node, const WidgetTree& tree) const
{
    (void)tree;
    node["control"] = {
        {"type", "text"},
        {"text", getText()},
    };
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

const FTextStyle& UIText::resolvedStyle(ReactiveBase::EDirtyLevel level, bool bTrackDependencies) const
{
    return resolvedStyleCache(*this, level,
                              [this](FTextStyle& style) {
                                  const bool bThemed = !_styleKey.empty() && getTree() && getTree()->getTheme()
                                                       && getTree()->getTheme()->find<FTextStyle>(_styleKey);
                                  if (bThemed) {
                                      return;
                                  }
                                  const bool bHasFontSize = _authoredStyle.is_object() && _authoredStyle.contains("fontSize");
                                  const bool bHasTextColor = _authoredStyle.is_object() && _authoredStyle.contains("textColor");
                                  const bool bHasFillColor = _authoredStyle.is_object() && _authoredStyle.contains("fillColor");
                                  if (!bHasFontSize) {
                                      style.fontSize = _fontSize;
                                  }
                                  if (!bHasTextColor) {
                                      style.textColor = _color;
                                  }
                                  if (!bHasFillColor) {
                                      style.fillColor = FBrush::solid(_color);
                                  }
                              },
                              bTrackDependencies);
}

glm::vec2 UIText::computeIntrinsicSize() const
{
    // Measure from the resolved text/style so the desired size matches paint
    // exactly when a binding is active. (Measure runs during layout, before
    // the paint walk, so get() here does not register a dependency; the Layout
    // edge is instead established by paintSelf at the same level.)
    const FTextStyle& style = resolvedStyle(ReactiveBase::EDirtyLevel::Layout, false);
    auto               font  = resolveTextFont(style);
    if (!font) {
        return {0.0f, 0.0f};
    }
    if (_bWrap) {
        // An explicit max width is a content constraint. With no explicit
        // constraint, intrinsic measurement stays single-line; the assigned
        // slot rect controls wrapping during paint. Do not use child `_size`
        // as an implicit wrap width.
        const std::string text = resolvedText(ReactiveBase::EDirtyLevel::Layout);
        const float maxWidth = _maxWrapWidth > 0.0f ? _maxWrapWidth : font->measureText(text);
        const auto  lines    = wrapText(text, font, maxWidth);
        return {maxWidth, static_cast<float>(lines.size()) * font->lineHeight};
    }
    const float w = font->measureText(resolvedText(ReactiveBase::EDirtyLevel::Layout));
    return {w, font->lineHeight};
}

glm::vec2 UIText::computeDesiredSize() const
{
    return computeIntrinsicSize();
}

} // namespace ya
