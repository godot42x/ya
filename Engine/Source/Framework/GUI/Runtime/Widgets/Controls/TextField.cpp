#include "GUI/Widgets/Controls/TextField.h"

#include "Core/KeyCode.h"

#include "Render/Resources/FontManager.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"

namespace ya
{

void UITextField::setError(bool error)
{
    if (_bError == error) {
        return;
    }
    _bError = error;
    invalidateProperty(EUIPropertyImpact::Paint);
}

void UITextField::paintSelf(UIFrameBuilder& builder)
{
    const FTextFieldStyle& style = resolvedStyle();
    builder.addBrush(_layoutRect, _bError ? style.errorFill : style.backgroundFill);

    auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, _fontSize);
    if (!font) {
        return;
    }

    const float fieldW      = _layoutRect.extent.x;
    const float caretOffset = font->measureText(_text.substr(0, _edit.caret));
    const float margin      = 4.0f;
    if (!_bFocused) {
        _scrollX = 0.0f;
    }
    else if (caretOffset > _scrollX + fieldW - margin) {
        _scrollX = caretOffset - fieldW + margin;
    }
    else if (caretOffset < _scrollX) {
        _scrollX = caretOffset;
    }

    textEditPaint(builder,
                  _layoutRect,
                  _text,
                  _edit,
                  font,
                  style.textColor,
                  style.caretColor,
                  style.selectionColor,
                  EWidgetAlignH::Left,
                  _scrollX,
                  _bFocused);
}

glm::vec2 UITextField::computeIntrinsicSize() const
{
    const auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, _fontSize);
    if (!font) {
        return {0.0f, 0.0f};
    }
    return {font->measureText(_text), font->lineHeight};
}

void UITextField::onFocusLost()
{
    _bFocused    = false;
    _bDragSelect = false;
    _edit.collapseToCaret();
    if (_onCommit) {
        _onCommit(_text);
    }
}

void UITextField::appendRuntimeDiagnostics(nlohmann::json& node, const WidgetTree& tree) const
{
    (void)tree;
    node["control"] = {
        {"type", "textField"},
        {"text", _text},
        {"cursor", getCursorIndex()},
        {"selectionStart", _edit.selectionStart()},
        {"selectionEnd", _edit.selectionEnd()},
    };
}

void UITextField::notifyTextChanged()
{
    invalidateProperty(EUIPropertyImpact::Paint);
    if (_onTextChanged) {
        _onTextChanged(_text);
    }
}

bool UITextField::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    const EEvent::T eventType = event.getEventType();

    if (eventType == EEvent::KeyPressed) {
        const auto& keyEvent = static_cast<const KeyPressedEvent&>(event);
        if (keyEvent._keyCode == EKey::Enter) {
            if (_onCommit) {
                _onCommit(_text);
            }
            return true;
        }
        const std::string before = _text;
        if (textEditHandleKey(_text, _edit, keyEvent, getTree(), _maxLength)) {
            if (_text != before) {
                notifyTextChanged();
            }
            else {
                invalidateProperty(EUIPropertyImpact::Paint);
            }
            return true;
        }
        return false;
    }

    if (eventType == EEvent::KeyTyped) {
        insertText(static_cast<const KeyTypedEvent&>(event).getText());
        return true;
    }

    if (eventType == EEvent::MouseButtonPressed) {
        const auto& mouse = static_cast<const MouseButtonPressedEvent&>(event);
        if (mouse.GetMouseButton() != EMouse::Left) {
            return false;
        }
        placeCaretAt(ctx.logicalPoint, false);
        _bDragSelect = true;
        if (WidgetTree* tree = getTree()) {
            tree->setFocus(this);
            tree->setPointerCapture(this);
        }
        return true;
    }

    if (eventType == EEvent::MouseMoved && (_bDragSelect || ctx.bViaCapture)) {
        placeCaretAt(ctx.logicalPoint, true);
        return true;
    }

    if (eventType == EEvent::MouseButtonReleased) {
        _bDragSelect = false;
        if (WidgetTree* tree = getTree()) {
            tree->releasePointerCapture(this);
        }
        return true;
    }

    return false;
}

void UITextField::insertText(const std::string& text)
{
    const std::string before = _text;
    textEditInsert(_text, _edit, text, _maxLength);
    if (_text != before) {
        notifyTextChanged();
    }
}

void UITextField::placeCaretAt(const glm::vec2& logicalPoint, bool bExtendSelection)
{
    auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, _fontSize);
    _edit.setCaret(textEditHitIndex(_text, font, _layoutRect, logicalPoint.x, EWidgetAlignH::Left, _scrollX),
                   bExtendSelection);
    invalidateProperty(EUIPropertyImpact::Paint);
}

} // namespace ya
