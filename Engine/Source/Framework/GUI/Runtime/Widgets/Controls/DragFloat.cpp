#include "GUI/Widgets/Controls/DragFloat.h"

#include "Core/KeyCode.h"
#include "Render/Resources/FontManager.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"

#include <algorithm>
#include <format>

namespace ya
{

void UIDragFloat::setValue(float value, bool bNotify)
{
    value = std::clamp(value, std::min(_min, _max), std::max(_min, _max));
    if (_value == value && !_bMixed) {
        return;
    }
    _value  = value;
    _bMixed = false;
    invalidateProperty(EUIPropertyImpact::Paint);
    if (bNotify && _onValueChanged) {
        _onValueChanged(_value);
    }
}

void UIDragFloat::setMixed(bool mixed)
{
    if (_bMixed == mixed) {
        return;
    }
    _bMixed = mixed;
    invalidateProperty(EUIPropertyImpact::Paint);
}

void UIDragFloat::setError(bool error)
{
    if (_bError == error) {
        return;
    }
    _bError = error;
    invalidateProperty(EUIPropertyImpact::Paint);
}

void UIDragFloat::adjustValue(float delta)
{
    setValue(_value + delta * _speed);
}

void UIDragFloat::beginEdit()
{
    _bEditing     = true;
    _editBuffer   = std::format("{:.{}f}", _value, _decimals);
    _bReplaceNext = true;
    if (WidgetTree* tree = getTree()) {
        tree->setFocus(this);
    }
}

void UIDragFloat::commitEdit()
{
    if (!_bEditing) {
        return;
    }
    _bEditing     = false;
    _bReplaceNext = false;
    try {
        const float parsed = std::stof(_editBuffer);
        setValue(parsed);
    }
    catch (...) {
        // Invalid text: keep the previous value.
    }
    _editBuffer.clear();
}

void UIDragFloat::cancelEdit()
{
    _bEditing     = false;
    _bReplaceNext = false;
    _editBuffer.clear();
}

void UIDragFloat::onFocusLost()
{
    commitEdit();
}

void UIDragFloat::paintSelf(UIFrameBuilder& builder)
{
    const FDragFloatStyle& style = resolvedStyle();
    const FBrush& fill = _bError ? style.errorFill
                       : _bDragging ? style.draggingFill
                                    : style.backgroundFill;
    builder.addBrush(_layoutRect, fill);
    const glm::vec4 outline = _bError ? style.errorBorderColor : style.borderColor;
    builder.addRectOutline(_layoutRect, outline, 1.0f);
    auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, _fontSize);
    if (!font) {
        return;
    }
    const std::string shown = _bEditing ? _editBuffer : (_bMixed ? std::string("—") : std::format("{:.{}f}", _value, _decimals));
    builder.addText(_layoutRect, shown, style.textColor, font, EWidgetAlignH::Center, EWidgetAlignV::Center);
    if (_bEditing) {
        const float textW  = font->measureText(shown);
        const float caretX = _layoutRect.pos.x + (_layoutRect.extent.x + textW) * 0.5f + 1.0f;
        const float caretY = _layoutRect.pos.y + (_layoutRect.extent.y - font->lineHeight) * 0.5f;
        builder.addSprite(Rect2D{.pos = {caretX, caretY}, .extent = {1.0f, font->lineHeight}},
                          style.textColor, nullptr);
    }
}

bool UIDragFloat::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    const EEvent::T eventType = event.getEventType();

    if (_bEditing) {
        if (eventType == EEvent::KeyTyped) {
            if (_bReplaceNext) {
                _editBuffer.clear();
                _bReplaceNext = false;
            }
            _editBuffer += static_cast<const KeyTypedEvent&>(event).getText();
            invalidateProperty(EUIPropertyImpact::Paint);
            return true;
        }
        if (eventType == EEvent::KeyPressed) {
            const auto& keyEvent = static_cast<const KeyPressedEvent&>(event);
            if (!keyEvent.bRepeat && keyEvent._keyCode == EKey::Backspace) {
                if (!_editBuffer.empty()) {
                    _editBuffer.pop_back();
                    invalidateProperty(EUIPropertyImpact::Paint);
                }
                return true;
            }
            if (!keyEvent.bRepeat && keyEvent._keyCode == EKey::Enter) {
                commitEdit();
                return true;
            }
            if (!keyEvent.bRepeat && keyEvent._keyCode == EKey::Escape) {
                cancelEdit();
                return true;
            }
        }
        return false;
    }

    if (eventType == EEvent::KeyPressed) {
        const auto& keyEvent = static_cast<const KeyPressedEvent&>(event);
        if (keyEvent._keyCode == EKey::Left) {
            adjustValue(-10.0f * (keyEvent._mod & 0x1 ? 10.0f : 1.0f));
            return true;
        }
        if (keyEvent._keyCode == EKey::Right) {
            adjustValue(10.0f * (keyEvent._mod & 0x1 ? 10.0f : 1.0f));
            return true;
        }
        return false;
    }

    const bool bPointInside = hitTestLayoutRect(ctx.logicalPoint);
    if (!ctx.bViaCapture && !bPointInside) {
        return false;
    }

    switch (eventType) {
    case EEvent::MouseButtonPressed: {
        // Double-click (event timestamps): a press within 400ms of the
        // previous one enters text edit mode; a single press starts the
        // capture drag.
        const uint64_t now     = event.getTimestampMs();
        const bool     bDouble = _bHasLastPress && (now - _lastPressTimeMs) < 400;
        _lastPressTimeMs = now;
        _bHasLastPress   = true;
        if (bDouble) {
            _bHasLastPress = false;
            beginEdit();
            return true;
        }
        _bDragging = true;
        _dragStart = ctx.logicalPoint;
        if (WidgetTree* tree = getTree()) {
            tree->setFocus(this);
            tree->setPointerCapture(this);
        }
        if (_onDragBegan) {
            _onDragBegan();
        }
        return true;
    }
    case EEvent::MouseMoved:
        if (_bDragging) {
            adjustValue((ctx.logicalPoint.x - _dragStart.x) * 0.1f);
            _dragStart = ctx.logicalPoint;
        }
        return true;
    case EEvent::MouseButtonReleased:
        if (_bDragging) {
            _bDragging = false;
            if (WidgetTree* tree = getTree()) {
                tree->releasePointerCapture(this);
            }
            if (_onDragEnded) {
                _onDragEnded();
            }
        }
        return true;
    default:
        return false;
    }
}

} // namespace ya
