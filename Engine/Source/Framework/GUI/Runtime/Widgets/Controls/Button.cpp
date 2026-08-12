#include "GUI/Widgets/Controls/Button.h"

#include "Core/KeyCode.h"
#include "Core/Log.h"

#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"

#include <algorithm>

namespace ya
{

void UIButton::layout(const Rect2D& parentRect)
{
    layoutAssigned(computeAnchorRect(parentRect));
}

void UIButton::layoutAssigned(const Rect2D& rect)
{
    _layoutRect = rect;

    // Content-slot arrangement: every visible content child fills the button
    // rect minus the content padding. Text children align themselves inside
    // that rect through their own paint h/v alignment; container content
    // (icon + text box) arranges its own children. Children never use anchor
    // math against the button (their rect is assigned).
    Rect2D contentRect = _layoutRect;
    contentRect.pos += _contentPadding;
    contentRect.extent -= _contentPadding * 2.0f;
    for (UIElement* child : getChildrenInPaintOrder()) {
        if (child->participatesInLayout()) {
            child->layoutAssigned(contentRect);
        }
    }
}

glm::vec2 UIButton::computeDesiredSize() const
{
    // Container packing always asks for the desired size; honor the explicit
    // size unless SizeToContent is enabled. With auto, size to the first
    // visible content child plus padding (Slate ContentControl).
    if (!_bAutoSize) {
        return _size;
    }
    for (UIElement* child : getChildrenInPaintOrder()) {
        if (!child->participatesInLayout()) {
            continue;
        }
        return child->computeDesiredSize() + _contentPadding * 2.0f;
    }
    return _size;
}

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
        // The press originates on the button (hit walk): the pointer is
        // over it, so hover follows the press even without a prior move.
        // Via capture the point may lie outside: then it is not hovered.
        _bHovered = bPointInside;
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
        // Hover reflects the release position: still inside keeps hover,
        // a drag-out release clears it (the next move re-arms it).
        _bHovered = bPointInside;
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
