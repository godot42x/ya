#include "GUI/Widgets/Controls/SelectableRow.h"

#include "Core/KeyCode.h"
#include "Core/Log.h"

#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"

namespace ya
{

struct FSelectableRowDragDrop
{
    static void install(UISelectableRow& widget)
    {
        auto drag = std::make_shared<UIDragSourceBehavior>();
        drag->bCapturePointerOnPress = true;
        drag->bBeginDragFromCapturedMove = true;
        drag->setPressedState = [](UIElement& owner, bool bPressed)
        {
            if (auto* row = dynamic_cast<UISelectableRow*>(&owner)) {
                row->_bPressed = bPressed;
            }
        };
        drag->operationFactory = [](UIElement& owner) -> UIDragDropOperationRef
        {
            auto* row = dynamic_cast<UISelectableRow*>(&owner);
            if (!row || !row->_bDraggable) {
                return nullptr;
            }
            return UIDragDropOperation::make(
                row->_dragPayload.empty() ? row->_itemId : row->_dragPayload,
                row->_dragGhostLabel.empty() ? row->_itemId : row->_dragGhostLabel,
                "ya.selectable.row");
        };
        widget.addBehavior(drag);

        auto drop = std::make_shared<UIDropTargetBehavior>();
        drop->canAccept = [](UIElement& owner, const UIDragDropOperation& operation, const glm::vec2& logicalPoint)
        {
            auto* row = dynamic_cast<UISelectableRow*>(&owner);
            return row && row->_bDraggable && owner.hitTestLayoutRect(logicalPoint) &&
                   operation.isType("ya.selectable.row") && !operation.payload.empty() &&
                   operation.payload != row->_itemId;
        };
        drop->handleDrop = [](UIElement& owner, const UIDragDropOperation& operation, const glm::vec2&)
        {
            if (auto* row = dynamic_cast<UISelectableRow*>(&owner)) {
                if (row->_onDropped && operation.isType("ya.selectable.row")) {
                    row->_onDropped(operation.payload);
                }
            }
        };
        drop->setHighlightState = [](UIElement& owner, bool bHighlight)
        {
            if (auto* row = dynamic_cast<UISelectableRow*>(&owner)) {
                row->_bDropHighlighted = bHighlight;
            }
        };
        widget.addBehavior(drop);
    }
};

UISelectableRow::UISelectableRow(std::string name) : UIElement(std::move(name), "selectable")
{
    _hitFilter  = EWidgetHitFilter::Stop;
    _focusPolicy = EWidgetFocusPolicy::Focusable;
    bindHostLayout(_contentLayout);
    FSelectableRowDragDrop::install(*this);
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
        _pressClickCount = static_cast<const MouseButtonPressedEvent&>(event).clickCount();
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
    case EEvent::MouseButtonReleased: {
        if (!_bPressed) {
            return false;
        }
        _bPressed = false;
        if (WidgetTree* tree = getTree()) {
            tree->releasePointerCapture(this);
        }
        (void)UIElement::handleInputEvent(event, ctx);
        const bool bActivates = _bActivateOnDoubleClick ? _pressClickCount >= 2 : true;
        if ((bPointInside || ctx.bViaCapture) && bActivates) {
            if (_onActivate) {
                _onActivate(_itemId);
            }
        }
        return true;
    }
    default:
        return false;
    }
}

void UISelectableRow::clearTransientInputState()
{
    _bHovered = false;
    _bPressed = false;
    _bDropHighlighted = false;
    _pressClickCount = 1;
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
