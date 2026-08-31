#include "GUI/Widgets/Controls/SelectableRow.h"

#include "Core/KeyCode.h"
#include "Core/Log.h"

#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"

namespace ya
{

struct FSelectableRowDragDropBehavior final : public UIDragSourceBehavior
{
    FSelectableRowDragDropBehavior()
    {
        bCapturePointerOnPress = true;
        bBeginDragFromCapturedMove = true;
        setPressedState = [](UIElement& owner, bool bPressed)
        {
            if (auto* row = dynamic_cast<UISelectableRow*>(&owner)) {
                row->_bPressed = bPressed;
            }
        };
        operationFactory = [](UIElement& owner) -> UIDragDropOperationRef
        {
            auto* row = dynamic_cast<UISelectableRow*>(&owner);
            if (!row || !row->_bDraggable) {
                return nullptr;
            }
            auto operation = std::make_shared<UIDragDropOperation>();
            operation->typeId = "selectable-row";
            operation->payload = row->_dragPayload.empty() ? row->_itemId : row->_dragPayload;
            operation->ghostLabel = row->_dragGhostLabel.empty() ? row->_itemId : row->_dragGhostLabel;
            return operation;
        };
    }
};

struct FSelectableRowDropTargetBehavior final : public UIDropTargetBehavior
{
    FSelectableRowDropTargetBehavior()
    {
        acceptPayload = [](UIElement& owner, const std::string& payload, const glm::vec2& logicalPoint)
        {
            auto* row = dynamic_cast<UISelectableRow*>(&owner);
            return row && row->_bDraggable && owner.hitTestLayoutRect(logicalPoint) && !payload.empty() && payload != row->_itemId;
        };
        handleDroppedPayload = [](UIElement& owner, const std::string& payload, const glm::vec2&)
        {
            if (auto* row = dynamic_cast<UISelectableRow*>(&owner)) {
                if (row->_onDropped) {
                    row->_onDropped(payload);
                }
            }
        };
        setHighlightState = [](UIElement& owner, bool bHighlight)
        {
            if (auto* row = dynamic_cast<UISelectableRow*>(&owner)) {
                row->_bDropHighlighted = bHighlight;
            }
        };
    }
};

UISelectableRow::UISelectableRow(std::string name) : UIElement(std::move(name), "selectable")
{
    _hitFilter  = EWidgetHitFilter::Stop;
    _focusPolicy = EWidgetFocusPolicy::Focusable;
    _contentLayout.setOwner(*this);
    addBehavior(std::make_shared<FSelectableRowDragDropBehavior>());
    addBehavior(std::make_shared<FSelectableRowDropTargetBehavior>());
}

void UISelectableRow::paintSelf(UIFrameBuilder& builder)
{
    const FSelectableRowStyle& style = resolvedStyle();
    const FBrush& fill = _bDropHighlighted ? style.selectedHoveredFill
                         : _bSelected      ? (_bHovered ? style.selectedHoveredFill : style.selectedFill)
                                           : (_bHovered ? style.hoveredFill : style.normalFill);
    if (fill.tintColor.a > 0.0f) {
        builder.addBrush(_layoutRect, fill);
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
        (void)UIElement::handleInputEvent(event, ctx);
        // Select on press: immediate feedback for pointer-driven navigation.
        if (_onSelect) {
            _onSelect(_itemId);
        }
        return true;
    case EEvent::MouseMoved:
        _bHovered = bPointInside;
        (void)UIElement::handleInputEvent(event, ctx);
        return true;
    case EEvent::MouseButtonReleased:
        if (!_bPressed) {
            return false;
        }
        _bPressed = false;
        if (WidgetTree* tree = getTree()) {
            tree->releasePointerCapture(this);
        }
        (void)UIElement::handleInputEvent(event, ctx);
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

void UISelectableRow::clearTransientInputState()
{
    _bHovered = false;
    _bPressed = false;
    _bDropHighlighted = false;
}

void UISelectableRow::layout(const Rect2D& parentRect)
{
    layoutAssigned(parentRect);
}

void UISelectableRow::layoutAssigned(const Rect2D& rect)
{
    setLayoutRect(rect);
    _contentLayout.arrange(*this, _layoutRect);
}

glm::vec2 UISelectableRow::computeDesiredSize() const
{
    return _contentLayout.measure(*this);
}

std::unique_ptr<UISlot> UISelectableRow::createSlotForChild(UIElement& child)
{
    return _contentLayout.createSlot(*this, child);
}

} // namespace ya
