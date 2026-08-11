#include "GUI/Widgets/Controls/SelectableRow.h"

#include "Core/KeyCode.h"
#include "Core/Log.h"

#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"

namespace ya
{

void UISelectableRow::paintSelf(UIFrameBuilder& builder)
{
    const glm::vec4 color = _bSelected
                                ? (_bHovered ? _selectedHoveredColor : _selectedColor)
                                : (_bHovered ? _hoveredColor : _normalColor);
    if (color.a > 0.0f) {
        builder.addSprite(_layoutRect, color, nullptr);
    }
}

bool UISelectableRow::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    const EEvent::T eventType = event.getEventType();

    // Keyboard events route to the focused row regardless of the pointer.
    if (eventType == EEvent::KeyPressed) {
        const auto& keyEvent = static_cast<const KeyPressedEvent&>(event);
        if (!keyEvent.bRepeat && (keyEvent._keyCode == EKey::Enter || keyEvent._keyCode == EKey::Space)) {
            if (_onActivate) {
                _onActivate(_itemId);
            }
        }
        return true; // the focused row consumes its activation keys
    }

    const bool bPointInside = hitTestLayoutRect(ctx.logicalPoint);
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
        // Select on press: immediate feedback for pointer-driven navigation.
        if (_onSelect) {
            _onSelect(_itemId);
        }
        return true;
    case EEvent::MouseButtonReleased:
        if (!_bPressed) {
            return false;
        }
        _bPressed = false;
        if (WidgetTree* tree = getTree()) {
            tree->releasePointerCapture(this);
        }
        if (bPointInside || ctx.bViaCapture) {
            if (_onActivate) {
                _onActivate(_itemId);
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

void UISelectableRow::clearTransientInputState()
{
    _bHovered = false;
    _bPressed = false;
}

glm::vec2 UISelectableRow::computeDesiredSize() const
{
    return _size;
}

} // namespace ya
