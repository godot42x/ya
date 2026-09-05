#include "GUI/Widgets/Controls/SpinBox.h"

#include "Core/KeyCode.h"
#include "Render/Resources/FontManager.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"

#include <algorithm>
#include <format>

namespace ya
{

void UISpinBox::setValue(float value)
{
    value = std::clamp(value, std::min(_min, _max), std::max(_min, _max));
    if (_value == value) {
        return;
    }
    _value = value;
    invalidateProperty(EUIPropertyImpact::Paint);
    if (_onValueChanged) {
        _onValueChanged(_value);
    }
}

void UISpinBox::stepBy(float multiplier)
{
    setValue(_value + _step * multiplier);
}

void UISpinBox::beginEdit()
{
    _bEditing     = true;
    _editBuffer   = std::format("{:.2f}", _value);
    _bReplaceNext = true;
    if (WidgetTree* tree = getTree()) {
        tree->setFocus(this);
    }
}

void UISpinBox::commitEdit()
{
    if (!_bEditing) {
        return;
    }
    _bEditing     = false;
    _bReplaceNext = false;
    try {
        setValue(std::stof(_editBuffer));
    }
    catch (...) {
        // Invalid text: keep the previous value.
    }
    _editBuffer.clear();
}

void UISpinBox::cancelEdit()
{
    _bEditing     = false;
    _bReplaceNext = false;
    _editBuffer.clear();
}

void UISpinBox::onFocusLost()
{
    commitEdit();
}

int UISpinBox::zoneFromPointer(float localX) const
{
    const float zoneWidth = 26.0f;
    if (localX < zoneWidth) {
        return 0; // minus
    }
    if (localX > _layoutRect.extent.x - zoneWidth) {
        return 1; // plus
    }
    return -1;
}

void UISpinBox::paintSelf(UIFrameBuilder& builder)
{
    const FSpinBoxStyle& style = resolvedStyle();
    builder.addBrush(_layoutRect, style.backgroundFill);
    builder.addRectOutline(_layoutRect, style.borderColor, 1.0f);
    const float zoneWidth = 26.0f;
    const Rect2D minusRect{.pos = _layoutRect.pos, .extent = {zoneWidth, _layoutRect.extent.y}};
    const Rect2D plusRect{.pos = {_layoutRect.pos.x + _layoutRect.extent.x - zoneWidth, _layoutRect.pos.y},
                          .extent = {zoneWidth, _layoutRect.extent.y}};
    builder.addBrush(minusRect, _hoveredZone == 0 ? style.buttonHoveredFill : style.buttonFill);
    builder.addBrush(plusRect, _hoveredZone == 1 ? style.buttonHoveredFill : style.buttonFill);

    auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, _fontSize);
    if (!font) {
        return;
    }
    builder.addText(minusRect, "-", style.textColor, font, EWidgetAlignH::Center, EWidgetAlignV::Center);
    builder.addText(plusRect, "+", style.textColor, font, EWidgetAlignH::Center, EWidgetAlignV::Center);
    const std::string shown = _bEditing ? _editBuffer : std::format("{:.2f}", _value);
    builder.addText(_layoutRect, shown, style.textColor, font, EWidgetAlignH::Center, EWidgetAlignV::Center);
    if (_bEditing) {
        const float textW  = font->measureText(shown);
        const float caretX = _layoutRect.pos.x + (_layoutRect.extent.x + textW) * 0.5f + 1.0f;
        const float caretY = _layoutRect.pos.y + (_layoutRect.extent.y - font->lineHeight) * 0.5f;
        builder.addSprite(Rect2D{.pos = {caretX, caretY}, .extent = {1.0f, font->lineHeight}},
                          style.textColor, nullptr);
    }
}

bool UISpinBox::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
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
        if (eventType == EEvent::MouseButtonPressed) {
            // A press on +/- while editing commits the edit first (an
            // unparseable buffer keeps the previous value), then steps —
            // the edit mode must never swallow the step buttons.
            const int zone = zoneFromPointer(ctx.logicalPoint.x - _layoutRect.pos.x);
            if (zone == 0 || zone == 1) {
                commitEdit();
                stepBy(zone == 0 ? -1.0f : 1.0f);
            }
            return true;
        }
        return false;
    }

    if (eventType == EEvent::KeyPressed) {
        const auto& keyEvent = static_cast<const KeyPressedEvent&>(event);
        if (!keyEvent.bRepeat && keyEvent._keyCode == EKey::Left) {
            stepBy(-1.0f);
            return true;
        }
        if (!keyEvent.bRepeat && keyEvent._keyCode == EKey::Right) {
            stepBy(1.0f);
            return true;
        }
        return false;
    }

    if (eventType == EEvent::MouseMoved) {
        const bool bInside = hitTestLayoutRect(ctx.logicalPoint);
        const int  zone    = bInside ? zoneFromPointer(ctx.logicalPoint.x - _layoutRect.pos.x) : -1;
        if (zone != _hoveredZone) {
            _hoveredZone = zone;
            markPaintDirty();
        }
        return bInside;
    }

    if (eventType == EEvent::MouseButtonPressed) {
        // Double-click (event timestamps): a press within 400ms of the
        // previous one enters text edit mode; a single press on the middle
        // value area also enters edit mode, while a press on +/- steps.
        const uint64_t now     = event.getTimestampMs();
        const bool     bDouble = _bHasLastPress && (now - _lastPressTimeMs) < 400;
        _lastPressTimeMs = now;
        _bHasLastPress   = true;
        const int zone = zoneFromPointer(ctx.logicalPoint.x - _layoutRect.pos.x);
        if (bDouble) {
            _bHasLastPress = false;
            beginEdit();
            return true;
        }
        if (zone == 0 || zone == 1) {
            if (WidgetTree* tree = getTree()) {
                tree->setFocus(this);
            }
            stepBy(zone == 0 ? -1.0f : 1.0f);
            return true;
        }
        // Middle value area: single click enters text edit mode.
        beginEdit();
        return true;
    }

    return false;
}

} // namespace ya
