#include "GUI/Widgets/Controls/Button.h"

#include "Core/KeyCode.h"
#include "Core/Log.h"

#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"

namespace ya
{

void UIButton::paintSelf(UIFrameBuilder& builder)
{
    const glm::vec4 color = _bPressed
                                ? _pressedColor
                                : (_bHovered
                                       ? _hoveredColor
                                       : (_bFocused ? _focusedColor : _normalColor));
    builder.addSprite(_layoutRect, color, nullptr);
}

bool UIButton::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    const EEvent::T eventType = event.getEventType();

    // Keyboard events route to the focused widget regardless of the pointer
    // position (the host dispatches them with a point outside any rect).
    if (eventType == EEvent::KeyPressed) {
        const auto& keyEvent = static_cast<const KeyPressedEvent&>(event);
        if (!keyEvent.bRepeat && (keyEvent._keyCode == EKey::Enter || keyEvent._keyCode == EKey::Space)) {
            YA_CORE_INFO("UIButton '{}' activated by keyboard", _name);
            if (_onClick) {
                _onClick();
            }
            return true;
        }
        // Other keys are not the button's business: they bubble as
        // NotHandled so the app layer can route them (e.g. list navigation).
        return false;
    }

    const bool bPointInside = hitTestLayoutRect(ctx.logicalPoint);
    // Pointer-captured events must reach the widget even outside its rect;
    // everything else requires the point to be inside.
    if (!ctx.bViaCapture && !bPointInside) {
        return false;
    }

    switch (eventType) {
    case EEvent::MouseButtonPressed:
        _bPressed = true;
        if (WidgetTree* tree = getTree()) {
            tree->setFocus(this);
            tree->setPointerCapture(this);
        }
        return true;
    case EEvent::MouseButtonReleased:
        if (!_bPressed) {
            return false; // stray release: no press session to complete
        }
        _bPressed = false;
        if (WidgetTree* tree = getTree()) {
            tree->releasePointerCapture(this);
        }
        // With pointer capture the release completes the click even when
        // the pointer left the widget (standard drag-release semantics).
        if (bPointInside || ctx.bViaCapture) {
            YA_CORE_INFO("UIButton '{}' clicked", _name);
            if (_onClick) {
                _onClick();
            }
        }
        return true;
    case EEvent::MouseMoved:
        _bHovered = bPointInside;
        return true;
    default:
        return false;
    }
}

void UIButton::clearTransientInputState()
{
    _bHovered = false;
    _bPressed = false;
}

} // namespace ya
