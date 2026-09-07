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
            return UIStringDragDropOperation::make(
                row->_dragPayload.empty() ? row->_itemId : row->_dragPayload,
                row->_dragGhostLabel.empty() ? row->_itemId : row->_dragGhostLabel,
                "ya.selectable.row");
        };
    }
};

struct FSelectableRowDropTargetBehavior final : public UIDropTargetBehavior
{
    FSelectableRowDropTargetBehavior()
    {
        canAccept = [](UIElement& owner, const UIDragDropOperation& operation, const glm::vec2& logicalPoint)
        {
            auto* row = dynamic_cast<UISelectableRow*>(&owner);
            const auto* textOp = operation.as<UIStringDragDropOperation>();
            return row && row->_bDraggable && owner.hitTestLayoutRect(logicalPoint) &&
                   textOp && !textOp->text.empty() && textOp->text != row->_itemId;
        };
        handleDrop = [](UIElement& owner, const UIDragDropOperation& operation, const glm::vec2&)
        {
            if (auto* row = dynamic_cast<UISelectableRow*>(&owner)) {
                const auto* textOp = operation.as<UIStringDragDropOperation>();
                if (row->_onDropped && textOp) {
                    row->_onDropped(textOp->text);
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
    bindHostLayout(_contentLayout);
    addBehavior(std::make_shared<FSelectableRowDragDropBehavior>());
    addBehavior(std::make_shared<FSelectableRowDropTargetBehavior>());
}

void UISelectableRow::paintSelf(UIFrameBuilder& builder)
{
    const FSelectableRowStyle& style = resolvedStyle();
    const FBrush& fill = resolveVisualFill(visualChrome(style),
                                           composeVisualFlags(_bHovered,
                                                              false,
                                                              false,
                                                              !isEnabledInTree(),
                                                              _bSelected,
                                                              false,
                                                              _bDropHighlighted));
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

glm::vec2 UISelectableRow::computeDesiredSize() const
{
    return _contentLayout.measure(*this);
}

std::unique_ptr<UISlot> UISelectableRow::createSlotForChild(UIElement& child)
{
    return _contentLayout.createSlot(*this, child);
}

} // namespace ya
