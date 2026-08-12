#include "GUI/Widgets/Controls/Slider.h"

#include "Core/KeyCode.h"
#include "Core/Log.h"

#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"

#include <algorithm>

namespace ya
{

void UISlider::setValue(float value)
{
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    if (clamped != _value) {
        _value = clamped;
        if (_onValueChanged) {
            _onValueChanged(_value);
        }
    }
}

float UISlider::valueFromPointer(float localX) const
{
    const float trackStart = _layoutRect.pos.x + _thumbSize.x * 0.5f;
    const float trackWidth = std::max(1.0f, _layoutRect.extent.x - _thumbSize.x);
    return (localX - trackStart) / trackWidth;
}

void UISlider::paintSelf(UIFrameBuilder& builder)
{
    const float trackHeight = 4.0f;
    Rect2D      track = _layoutRect;
    track.pos.y += (_layoutRect.extent.y - trackHeight) * 0.5f;
    track.extent = {_layoutRect.extent.x, trackHeight};
    builder.addSprite(track, _trackColor, nullptr);

    // Fill up to the thumb center.
    const float thumbCenterX = _layoutRect.pos.x + _value * _layoutRect.extent.x;
    Rect2D      fill = track;
    fill.extent.x = std::max(0.0f, thumbCenterX - track.pos.x);
    if (fill.extent.x > 0.0f) {
        builder.addSprite(fill, _fillColor, nullptr);
    }

    Rect2D thumb{
        .pos    = {thumbCenterX - _thumbSize.x * 0.5f,
                   _layoutRect.pos.y + (_layoutRect.extent.y - _thumbSize.y) * 0.5f},
        .extent = _thumbSize,
    };
    builder.addSprite(thumb, _thumbColor, nullptr);
}

bool UISlider::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    const EEvent::T eventType = event.getEventType();

    if (eventType == EEvent::KeyPressed) {
        const auto& keyEvent = static_cast<const KeyPressedEvent&>(event);
        if (keyEvent.bRepeat) {
            return true;
        }
        const float bigStep = _step * 10.0f;
        switch (keyEvent._keyCode) {
        case EKey::Left:
            setValue(_value - (keyEvent.isShiftPressed() ? bigStep : _step));
            return true;
        case EKey::Right:
            setValue(_value + (keyEvent.isShiftPressed() ? bigStep : _step));
            return true;
        case EKey::Home:
            setValue(0.0f);
            return true;
        case EKey::End:
            setValue(1.0f);
            return true;
        case EKey::Space:
        case EKey::Enter:
            setValue(_value + _step);
            return true;
        default:
            return false;
        }
    }

    const bool bPointInside = hitTestLayoutRect(ctx.logicalPoint);
    if (!ctx.bViaCapture && !bPointInside) {
        return false;
    }

    switch (eventType) {
    case EEvent::MouseButtonPressed:
        _bDragging = true;
        setValue(valueFromPointer(ctx.logicalPoint.x));
        if (WidgetTree* tree = getTree()) {
            tree->setFocus(this);
            tree->setPointerCapture(this);
        }
        return true;
    case EEvent::MouseMoved:
        if (_bDragging) {
            setValue(valueFromPointer(ctx.logicalPoint.x));
            return true;
        }
        return false;
    case EEvent::MouseButtonReleased:
        if (!_bDragging) {
            return false;
        }
        _bDragging = false;
        setValue(valueFromPointer(ctx.logicalPoint.x));
        if (WidgetTree* tree = getTree()) {
            tree->releasePointerCapture(this);
        }
        return true;
    default:
        return false;
    }
}

} // namespace ya
