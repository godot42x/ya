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
    if (_bDragging) {
        _bDragging = false;
        if (WidgetTree* tree = getTree()) {
            tree->releasePointerCapture(this);
        }
        if (_onDragEnded) {
            _onDragEnded();
        }
    }
    _bEditing   = true;
    _editBuffer = std::format("{:.{}f}", _value, _decimals);
    _edit.selectAll(_editBuffer.size());
    if (WidgetTree* tree = getTree()) {
        tree->setFocus(this);
    }
}

void UIDragFloat::commitEdit()
{
    if (!_bEditing) {
        return;
    }
    _bEditing = false;
    _edit     = {};
    try {
        const float parsed = std::stof(_editBuffer);
        setValue(parsed);
    }
    catch (...) {
    }
    _editBuffer.clear();
}

void UIDragFloat::cancelEdit()
{
    _bEditing = false;
    _edit     = {};
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
                       : _bHovered ? style.hoveredFill
                                    : style.backgroundFill;
    builder.addBrush(_layoutRect, fill);
    const glm::vec4 outline = _bError ? style.errorBorderColor : style.borderColor;
    builder.addRectOutline(insetRect(_layoutRect, 1.0f), outline, 1.0f);
    auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, _fontSize);
    if (!font) {
        return;
    }
    Rect2D inner = _layoutRect;
    inner.pos += style.padding;
    inner.extent = glm::max(inner.extent - style.padding * 2.0f, glm::vec2(0.0f));
    const std::string shown = _bEditing ? _editBuffer
                                        : (_bMixed ? std::string("—")
                                                   : std::format("{:.{}f}", _value, _decimals));
    if (_bEditing) {
        textEditPaint(builder,
                      inner,
                      shown,
                      _edit,
                      font,
                      style.textColor,
                      style.textColor,
                      kTextEditSelectionColor,
                      EWidgetAlignH::Center,
                      0.0f,
                      true);
        return;
    }
    builder.addText(inner, shown, style.textColor, font, EWidgetAlignH::Center, EWidgetAlignV::Center);
}

bool UIDragFloat::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    const EEvent::T eventType = event.getEventType();

    if (_bEditing) {
        constexpr uint32_t kEditMaxLength = 32;
        if (eventType == EEvent::KeyTyped) {
            textEditInsert(_editBuffer, _edit, static_cast<const KeyTypedEvent&>(event).getText(), kEditMaxLength);
            invalidateProperty(EUIPropertyImpact::Paint);
            return true;
        }
        if (eventType == EEvent::KeyPressed) {
            const auto& keyEvent = static_cast<const KeyPressedEvent&>(event);
            if (keyEvent._keyCode == EKey::Enter) {
                commitEdit();
                return true;
            }
            if (keyEvent._keyCode == EKey::Escape) {
                cancelEdit();
                return true;
            }
            if (textEditHandleKey(_editBuffer, _edit, keyEvent, getTree(), kEditMaxLength)) {
                invalidateProperty(EUIPropertyImpact::Paint);
                return true;
            }
            return false;
        }
        if (eventType == EEvent::MouseButtonPressed) {
            const auto& mouse = static_cast<const MouseButtonPressedEvent&>(event);
            if (mouse.GetMouseButton() != EMouse::Left) {
                return false;
            }
            auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, _fontSize);
            _edit.setCaret(textEditHitIndex(_editBuffer, font, _layoutRect, ctx.logicalPoint.x,
                                            EWidgetAlignH::Center, 0.0f),
                           false);
            if (WidgetTree* tree = getTree()) {
                tree->setPointerCapture(this);
            }
            invalidateProperty(EUIPropertyImpact::Paint);
            return true;
        }
        if (eventType == EEvent::MouseMoved && ctx.bViaCapture) {
            auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, _fontSize);
            _edit.setCaret(textEditHitIndex(_editBuffer, font, _layoutRect, ctx.logicalPoint.x,
                                            EWidgetAlignH::Center, 0.0f),
                           true);
            invalidateProperty(EUIPropertyImpact::Paint);
            return true;
        }
        if (eventType == EEvent::MouseButtonReleased) {
            if (WidgetTree* tree = getTree()) {
                tree->releasePointerCapture(this);
            }
            return true;
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
