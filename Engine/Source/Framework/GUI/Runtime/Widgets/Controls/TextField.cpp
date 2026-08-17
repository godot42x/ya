#include "GUI/Widgets/Controls/TextField.h"

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

/// Previous code-point boundary at or before `byteIndex`.
size_t previousCodePoint(const std::string& text, size_t byteIndex)
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

/// Next code-point boundary at or after `byteIndex`.
size_t nextCodePoint(const std::string& text, size_t byteIndex)
{
    size_t    offset = std::min(byteIndex, text.size());
    uint32_t  codePoint = 0;
    if (utf8::decodeNext(text, offset, codePoint)) {
        return offset;
    }
    return byteIndex;
}

} // namespace

void UITextField::paintSelf(UIFrameBuilder& builder)
{
    builder.addSprite(_layoutRect, _backgroundColor, nullptr);

    auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, _fontSize);
    if (!font) {
        return;
    }

    builder.pushClip(_layoutRect);
    builder.addText(_layoutRect, _text, _textColor, font, EWidgetAlignH::Left, EWidgetAlignV::Center);
    if (_bFocused) {
        const float caretX = _layoutRect.pos.x + font->measureText(_text.substr(0, _cursorIndex));
        const float caretY = _layoutRect.pos.y + (_layoutRect.extent.y - font->lineHeight) * 0.5f;
        builder.addSprite(Rect2D{.pos = {caretX, caretY}, .extent = {1.0f, font->lineHeight}},
                          _caretColor, nullptr);
    }
    builder.popClip();
}

void UITextField::onFocusLost()
{
    _bFocused = false;
    if (_onCommit) {
        _onCommit(_text);
    }
}

bool UITextField::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    const EEvent::T eventType = event.getEventType();

    if (eventType == EEvent::KeyPressed) {
        const auto& keyEvent = static_cast<const KeyPressedEvent&>(event);
        switch (keyEvent._keyCode) {
        case EKey::Backspace:
            erasePreviousCodePoint();
            return true;
        case EKey::Left:
            moveCursorByCodePoint(-1);
            return true;
        case EKey::Right:
            moveCursorByCodePoint(1);
            return true;
        case EKey::Home:
            _cursorIndex = 0;
            return true;
        case EKey::End:
            _cursorIndex = _text.size();
            return true;
        case EKey::Enter:
            if (_onCommit) {
                _onCommit(_text);
            }
            return true;
        default:
            return false; // other keys bubble as NotHandled
        }
    }

    if (eventType == EEvent::KeyTyped) {
        insertText(static_cast<const KeyTypedEvent&>(event).getText());
        return true;
    }

    if (eventType == EEvent::MouseButtonPressed) {
        placeCaretAt(ctx.logicalPoint);
        if (WidgetTree* tree = getTree()) {
            tree->setFocus(this);
        }
        return true;
    }

    return false;
}

void UITextField::moveCursorByCodePoint(int direction)
{
    if (direction < 0) {
        _cursorIndex = previousCodePoint(_text, _cursorIndex);
    }
    else {
        _cursorIndex = nextCodePoint(_text, _cursorIndex);
    }
}

void UITextField::erasePreviousCodePoint()
{
    const size_t start = previousCodePoint(_text, _cursorIndex);
    if (start == _cursorIndex) {
        return;
    }
    _text.erase(start, _cursorIndex - start);
    _cursorIndex = start;
    if (_onTextChanged) {
        _onTextChanged(_text);
    }
}

void UITextField::insertText(const std::string& text)
{
    if (text.empty()) {
        return;
    }
    // Single-line field: strip newlines / tabs from pasted input.
    std::string clean;
    clean.reserve(text.size());
    for (const char ch : text) {
        if (ch != '\n' && ch != '\r' && ch != '\t') {
            clean.push_back(ch);
        }
    }
    if (clean.empty()) {
        return;
    }
    if (_text.size() + clean.size() > _maxLength) {
        clean.resize(_maxLength - std::min<size_t>(_maxLength, _text.size()));
    }
    if (clean.empty()) {
        return;
    }
    _text.insert(_cursorIndex, clean);
    _cursorIndex += clean.size();
    if (_onTextChanged) {
        _onTextChanged(_text);
    }
}

void UITextField::placeCaretAt(const glm::vec2& logicalPoint)
{
    auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, _fontSize);
    if (!font) {
        _cursorIndex = _text.size();
        return;
    }

    // Walk code points; pick the boundary closest to the click x.
    const float localX = logicalPoint.x - _layoutRect.pos.x;
    size_t      best   = 0;
    float       bestDistance = std::numeric_limits<float>::max();
    size_t      offset = 0;
    while (offset <= _text.size()) {
        const float width = font->measureText(_text.substr(0, offset));
        const float distance = std::abs(width - localX);
        if (distance < bestDistance) {
            bestDistance = distance;
            best         = offset;
        }
        if (offset == _text.size()) {
            break;
        }
        offset = nextCodePoint(_text, offset);
    }
    _cursorIndex = best;
}

} // namespace ya
