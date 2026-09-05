#include "GUI/Widgets/Controls/TextEdit.h"

#include "Core/Event.h"
#include "Core/KeyCode.h"
#include "Render/Resources/FontManager.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"

#include <algorithm>
#include <limits>

namespace ya
{

namespace
{

float textOriginX(const std::string&           text,
                  const std::shared_ptr<Font>& font,
                  const Rect2D&                rect,
                  EWidgetAlignH                alignH,
                  float                        scrollX)
{
    float origin = rect.pos.x - scrollX;
    const float textW = font->measureText(text);
    if (alignH == EWidgetAlignH::Center) {
        origin += (rect.extent.x - textW) * 0.5f;
    }
    else if (alignH == EWidgetAlignH::Right) {
        origin += rect.extent.x - textW;
    }
    return origin;
}

void copyToClipboard(WidgetTree* tree, const std::string& text, const FTextEditState& state)
{
    if (!tree) {
        return;
    }
    if (state.hasSelection()) {
        tree->setClipboardText(text.substr(state.selectionStart(),
                                           state.selectionEnd() - state.selectionStart()));
        return;
    }
    tree->setClipboardText(text);
}

} // namespace

size_t textEditPreviousCodePoint(const std::string& text, size_t byteIndex)
{
    byteIndex = std::min(byteIndex, text.size());
    if (byteIndex == 0 || text.empty()) {
        return 0;
    }
    size_t i = byteIndex;
    do {
        --i;
    } while (i > 0 && (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80);
    return i;
}

size_t textEditNextCodePoint(const std::string& text, size_t byteIndex)
{
    size_t   offset    = std::min(byteIndex, text.size());
    uint32_t codePoint = 0;
    if (utf8::decodeNext(text, offset, codePoint)) {
        return offset;
    }
    return byteIndex;
}

std::string textEditSanitizeSingleLine(std::string_view text)
{
    std::string clean;
    clean.reserve(text.size());
    for (const char ch : text) {
        if (ch != '\n' && ch != '\r' && ch != '\t') {
            clean.push_back(ch);
        }
    }
    return clean;
}

bool textEditDeleteSelection(std::string& text, FTextEditState& state)
{
    state.clamp(text.size());
    if (!state.hasSelection()) {
        return false;
    }
    const size_t start = state.selectionStart();
    const size_t end   = state.selectionEnd();
    text.erase(start, end - start);
    state.setCaret(start, false);
    return true;
}

void textEditInsert(std::string&     text,
                    FTextEditState&  state,
                    std::string_view insert,
                    uint32_t         maxLength)
{
    std::string clean = textEditSanitizeSingleLine(insert);
    if (clean.empty()) {
        return;
    }
    textEditDeleteSelection(text, state);
    if (text.size() >= maxLength) {
        return;
    }
    const size_t room = static_cast<size_t>(maxLength) - text.size();
    if (clean.size() > room) {
        clean.resize(room);
    }
    if (clean.empty()) {
        return;
    }
    state.clamp(text.size());
    text.insert(state.caret, clean);
    state.setCaret(state.caret + clean.size(), false);
}

void textEditBackspace(std::string& text, FTextEditState& state)
{
    if (textEditDeleteSelection(text, state)) {
        return;
    }
    const size_t start = textEditPreviousCodePoint(text, state.caret);
    if (start == state.caret) {
        return;
    }
    text.erase(start, state.caret - start);
    state.setCaret(start, false);
}

void textEditDeleteForward(std::string& text, FTextEditState& state)
{
    if (textEditDeleteSelection(text, state)) {
        return;
    }
    const size_t end = textEditNextCodePoint(text, state.caret);
    if (end == state.caret) {
        return;
    }
    text.erase(state.caret, end - state.caret);
}

void textEditMoveByCodePoint(const std::string& text,
                             FTextEditState&    state,
                             int                direction,
                             bool               bExtend)
{
    state.clamp(text.size());
    if (!bExtend && state.hasSelection()) {
        state.setCaret(direction < 0 ? state.selectionStart() : state.selectionEnd(), false);
        return;
    }
    const size_t next = direction < 0 ? textEditPreviousCodePoint(text, state.caret)
                                      : textEditNextCodePoint(text, state.caret);
    state.setCaret(next, bExtend);
}

void textEditMoveHome(FTextEditState& state, bool bExtend)
{
    state.setCaret(0, bExtend);
}

void textEditMoveEnd(const std::string& text, FTextEditState& state, bool bExtend)
{
    state.setCaret(text.size(), bExtend);
}

bool textEditHandleKey(std::string&           text,
                       FTextEditState&        state,
                       const KeyPressedEvent& key,
                       WidgetTree*            tree,
                       uint32_t               maxLength)
{
    state.clamp(text.size());
    if (key.isPrimaryModifierPressed() && !key.isAltPressed() && !key.isShiftPressed()) {
        if (key.bRepeat) {
            switch (key._keyCode) {
            case EKey::K_C:
            case EKey::K_X:
            case EKey::K_V:
            case EKey::K_A:
                return false;
            default:
                break;
            }
        }
        switch (key._keyCode) {
        case EKey::K_C:
            copyToClipboard(tree, text, state);
            return true;
        case EKey::K_X:
            copyToClipboard(tree, text, state);
            if (state.hasSelection()) {
                textEditDeleteSelection(text, state);
            }
            else if (!text.empty()) {
                text.clear();
                state.setCaret(0, false);
            }
            return true;
        case EKey::K_V:
            if (tree) {
                textEditInsert(text, state, tree->getClipboardText(), maxLength);
            }
            return true;
        case EKey::K_A:
            state.selectAll(text.size());
            return true;
        default:
            break;
        }
    }

    switch (key._keyCode) {
    case EKey::Backspace:
        textEditBackspace(text, state);
        return true;
    case EKey::Delete:
        textEditDeleteForward(text, state);
        return true;
    case EKey::Left:
        textEditMoveByCodePoint(text, state, -1, key.isShiftPressed());
        return true;
    case EKey::Right:
        textEditMoveByCodePoint(text, state, 1, key.isShiftPressed());
        return true;
    case EKey::Home:
        textEditMoveHome(state, key.isShiftPressed());
        return true;
    case EKey::End:
        textEditMoveEnd(text, state, key.isShiftPressed());
        return true;
    default:
        return false;
    }
}

size_t textEditHitIndex(const std::string&           text,
                        const std::shared_ptr<Font>& font,
                        const Rect2D&                rect,
                        float                        logicalX,
                        EWidgetAlignH                alignH,
                        float                        scrollX)
{
    if (!font) {
        return text.size();
    }
    const float origin = textOriginX(text, font, rect, alignH, scrollX);
    const float localX = logicalX - origin;
    size_t      best   = 0;
    float       bestDistance = std::numeric_limits<float>::max();
    size_t      offset = 0;
    while (offset <= text.size()) {
        const float width    = font->measureText(text.substr(0, offset));
        const float distance = std::abs(width - localX);
        if (distance < bestDistance) {
            bestDistance = distance;
            best         = offset;
        }
        if (offset == text.size()) {
            break;
        }
        offset = textEditNextCodePoint(text, offset);
    }
    return best;
}

void textEditPaint(UIFrameBuilder&             builder,
                   const Rect2D&               rect,
                   const std::string&          text,
                   const FTextEditState&       state,
                   const std::shared_ptr<Font>& font,
                   const glm::vec4&            textColor,
                   const glm::vec4&            caretColor,
                   const glm::vec4&            selectionColor,
                   EWidgetAlignH               alignH,
                   float                       scrollX,
                   bool                        bShowCaret)
{
    if (!font) {
        return;
    }
    const float originX = textOriginX(text, font, rect, alignH, scrollX);
    const float textY   = rect.pos.y + (rect.extent.y - font->lineHeight) * 0.5f;

    if (state.hasSelection()) {
        const float x0 = originX + font->measureText(text.substr(0, state.selectionStart()));
        const float x1 = originX + font->measureText(text.substr(0, state.selectionEnd()));
        builder.addSprite(Rect2D{.pos = {x0, textY}, .extent = {std::max(1.0f, x1 - x0), font->lineHeight}},
                          selectionColor,
                          nullptr);
    }

    Rect2D textRect = rect;
    textRect.pos.x -= scrollX;
    builder.addText(textRect, text, textColor, font, alignH, EWidgetAlignV::Center);

    if (bShowCaret) {
        const float caretX = originX + font->measureText(text.substr(0, std::min(state.caret, text.size())));
        builder.addSprite(Rect2D{.pos = {caretX, textY}, .extent = {1.0f, font->lineHeight}},
                          caretColor,
                          nullptr);
    }
}

} // namespace ya
