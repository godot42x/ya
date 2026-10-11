#include "GUI/Widgets/Controls/Text.h"

#include "Render/Resources/FontManager.h"
#include "GUI/Widgets/UIFrameSnapshot.h"

#include <algorithm>
#include <cstdint>
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
        // so the incremental cache stays granular. Never paint wider than the
        // arranged rect, and honor an explicit wrap width when it is tighter.
        float maxWidth = _layoutRect.extent.x;
        if (_maxWrapWidth > 0.0f) {
            maxWidth = maxWidth > 0.0f ? std::min(maxWidth, _maxWrapWidth) : _maxWrapWidth;
        }
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

namespace
{

size_t utf8Next(const std::string& text, size_t i)
{
    size_t next = i + 1;
    while (next < text.size() && (static_cast<unsigned char>(text[next]) & 0xC0) == 0x80) {
        ++next;
    }
    return next;
}

uint32_t utf8CodePoint(const std::string& text, size_t i, size_t next)
{
    const auto* bytes = reinterpret_cast<const unsigned char*>(text.data());
    const size_t count = next - i;
    if (count == 1) {
        return bytes[i];
    }
    if (count == 2) {
        return (static_cast<uint32_t>(bytes[i] & 0x1F) << 6) | (bytes[i + 1] & 0x3F);
    }
    if (count == 3) {
        return (static_cast<uint32_t>(bytes[i] & 0x0F) << 12) |
               (static_cast<uint32_t>(bytes[i + 1] & 0x3F) << 6) | (bytes[i + 2] & 0x3F);
    }
    if (count == 4 && next <= text.size()) {
        return (static_cast<uint32_t>(bytes[i] & 0x07) << 18) |
               (static_cast<uint32_t>(bytes[i + 1] & 0x3F) << 12) |
               (static_cast<uint32_t>(bytes[i + 2] & 0x3F) << 6) | (bytes[i + 3] & 0x3F);
    }
    return bytes[i];
}

// CJK, kana, hangul, and fullwidth forms break per character. Latin stays
// in a word so "three" is not split into "t" / "hree".
bool isCjkBreak(uint32_t codePoint)
{
    return (codePoint >= 0x2E80 && codePoint <= 0x9FFF) || (codePoint >= 0xF900 && codePoint <= 0xFAFF) ||
           (codePoint >= 0x3000 && codePoint <= 0x303F) || (codePoint >= 0x3040 && codePoint <= 0x30FF) ||
           (codePoint >= 0xAC00 && codePoint <= 0xD7AF) || (codePoint >= 0xFF00 && codePoint <= 0xFFEF);
}

enum class EWrapPiece
{
    Word,
    Space,
    Break,
};

struct FWrapPiece
{
    std::string text;
    EWrapPiece  kind = EWrapPiece::Word;
};

} // namespace

std::vector<std::string> UIText::wrapText(const std::string& text,
                                          const std::shared_ptr<Font>& font,
                                          float maxWidth)
{
    std::vector<std::string> lines;
    if (!font || maxWidth <= 0.0f) {
        lines.push_back(text);
        return lines;
    }

    std::vector<FWrapPiece> pieces;
    size_t                  i = 0;
    while (i < text.size()) {
        const size_t next = utf8Next(text, i);
        if (next == i + 1 && text[i] == '\r') {
            i = next;
            continue;
        }
        // A newline is a hard break. Paint and the desired height both count
        // that line.
        if (next == i + 1 && text[i] == '\n') {
            pieces.push_back(FWrapPiece{.text = {}, .kind = EWrapPiece::Break});
            i = next;
            continue;
        }
        const uint32_t codePoint = utf8CodePoint(text, i, next);
        if (codePoint == ' ' || codePoint == '\t') {
            pieces.push_back(FWrapPiece{.text = text.substr(i, next - i), .kind = EWrapPiece::Space});
            i = next;
            continue;
        }
        if (isCjkBreak(codePoint)) {
            pieces.push_back(FWrapPiece{.text = text.substr(i, next - i), .kind = EWrapPiece::Word});
            i = next;
            continue;
        }
        size_t end = next;
        while (end < text.size()) {
            const size_t step = utf8Next(text, end);
            if (step == end + 1 && (text[end] == '\n' || text[end] == '\r')) {
                break;
            }
            const uint32_t ahead = utf8CodePoint(text, end, step);
            if (ahead == ' ' || ahead == '\t' || isCjkBreak(ahead)) {
                break;
            }
            end = step;
        }
        pieces.push_back(FWrapPiece{.text = text.substr(i, end - i), .kind = EWrapPiece::Word});
        i = end;
    }

    std::string current;
    auto        pushLine = [&]() {
        lines.push_back(current);
        current.clear();
    };
    // A word wider than the line is split only once it is alone on that line.
    auto appendWord = [&](const std::string& word) {
        if (!current.empty() && font->measureText(current + word) > maxWidth) {
            pushLine();
        }
        if (current.empty() && font->measureText(word) > maxWidth) {
            size_t at = 0;
            while (at < word.size()) {
                const size_t      step  = utf8Next(word, at);
                const std::string glyph = word.substr(at, step - at);
                if (!current.empty() && font->measureText(current + glyph) > maxWidth) {
                    pushLine();
                }
                current += glyph;
                at = step;
            }
            return;
        }
        current += word;
    };

    for (const FWrapPiece& piece : pieces) {
        if (piece.kind == EWrapPiece::Break) {
            pushLine();
            continue;
        }
        // Spaces that do not fit are dropped, so the next line does not
        // start with the gap that ended the previous one.
        if (piece.kind == EWrapPiece::Space) {
            if (!current.empty() && font->measureText(current + piece.text) <= maxWidth) {
                current += piece.text;
            }
            else if (!current.empty()) {
                pushLine();
            }
            continue;
        }
        appendWord(piece.text);
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
    const std::string text = resolvedText(ReactiveBase::EDirtyLevel::Layout);
    // An empty label takes no space, so an unused choice row does not leave a gap.
    if (text.empty()) {
        return {0.0f, 0.0f};
    }
    if (_bWrap) {
        // An explicit max width is a content constraint. With no explicit
        // constraint, wrapping still breaks on newlines; the assigned slot
        // rect controls soft wrapping during paint. Do not use child `_size`
        // as an implicit wrap width.
        const float maxWidth = _maxWrapWidth > 0.0f ? _maxWrapWidth : font->measureText(text);
        const auto  lines    = wrapText(text, font, maxWidth);
        float       width    = 0.0f;
        for (const std::string& line : lines) {
            width = std::max(width, font->measureText(line));
        }
        return {width, static_cast<float>(std::max<size_t>(lines.size(), 1)) * font->lineHeight};
    }
    size_t lineCount = 1;
    for (unsigned char byte : text) {
        if (byte == '\n') {
            ++lineCount;
        }
    }
    return {font->measureText(text), static_cast<float>(lineCount) * font->lineHeight};
}

glm::vec2 UIText::computeDesiredSize() const
{
    return computeIntrinsicSize();
}

} // namespace ya
