#include "GUI/Widgets/Controls/SelectableRow.h"

#include "Core/KeyCode.h"
#include "Core/Log.h"

#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"

namespace ya
{

void UISelectableRow::paintSelf(UIFrameBuilder& builder)
{
    if (_bDropHighlighted) {
        builder.addSprite(_layoutRect, _selectedHoveredColor, nullptr);
        return;
    }
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
            return true;
        }
        // Other keys (arrows etc.) bubble as NotHandled for the app layer.
        return false;
    }

    const bool bPointInside = hitTestLayoutRect(ctx.logicalPoint);
    if (!ctx.bViaCapture && !bPointInside) {
        return false;
    }

    switch (eventType) {
    case EEvent::MouseButtonPressed:
        _bPressed = true;
        _pressPoint = ctx.logicalPoint;
        if (WidgetTree* tree = getTree()) {
            tree->setFocus(this);
            tree->setPointerCapture(this);
        }
        // Select on press: immediate feedback for pointer-driven navigation.
        if (_onSelect) {
            _onSelect(_itemId);
        }
        return true;
    case EEvent::MouseMoved:
        // Drag initiation: press then move past a small threshold. The row
        // hands the session to the tree (which owns the ghost + drop target
        // from here on) and releases its own capture.
        if (_bDraggable && _bPressed && getTree() && !getTree()->isDragging()) {
            const float dist = glm::length(ctx.logicalPoint - _pressPoint);
            if (dist > 6.0f) {
                WidgetTree* tree = getTree();
                tree->releasePointerCapture(this);
                tree->beginDrag(this,
                                _dragPayload.empty() ? _itemId : _dragPayload,
                                _dragGhostLabel.empty() ? _itemId : _dragGhostLabel);
                _bPressed = false;
            }
        }
        _bHovered = bPointInside;
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
    default:
        return false;
    }
}

bool UISelectableRow::canAcceptDrop(const std::string& payload, const glm::vec2&)
{
    return _bDraggable && !payload.empty() && payload != _itemId;
}

void UISelectableRow::onDrop(const std::string& payload, const glm::vec2&)
{
    _bDropHighlighted = false;
    if (_onDropped) {
        _onDropped(payload);
    }
}

void UISelectableRow::clearTransientInputState()
{
    _bHovered = false;
    _bPressed = false;
    _bDropHighlighted = false;
}

glm::vec2 UISelectableRow::computeDesiredSize() const
{
    return _size;
}

} // namespace ya
