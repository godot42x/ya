#include "GUI/Widgets/UIBehavior.h"

#include "Core/Event.h"
#include "GUI/Widgets/UIElement.h"
#include "GUI/Widgets/WidgetTree.h"
#include <glm/glm.hpp>

namespace ya
{

void UIBehavior::onAttached(UIElement& owner)
{
    _owner = &owner;
}

void UIBehavior::onDetached(UIElement& owner)
{
    if (_owner == &owner) {
        _owner = nullptr;
    }
}

void UIBehavior::invalidateOwnerPaint() const
{
    if (_owner) {
        _owner->markPaintDirty();
    }
}

void UIBehavior::invalidateOwnerLayout() const
{
    if (_owner) {
        _owner->markLayoutDirty();
    }
}

void UIBehavior::invalidateOwnerSubtree() const
{
    if (_owner) {
        _owner->invalidateSubtree();
    }
}

bool UIDragSourceBehavior::handleInputEvent(UIElement& owner, const Event& event, const WidgetEventContext& ctx)
{
    switch (event.getEventType()) {
    case EEvent::MouseButtonPressed:
        _bPressed = true;
        _pressPoint = ctx.logicalPoint;
        if (setPressedState) {
            setPressedState(owner, true);
        }
        if (bCapturePointerOnPress) {
            if (WidgetTree* tree = owner.getTree()) {
                tree->setPointerCapture(&owner);
            }
        }
        return true;
    case EEvent::MouseMoved:
        if (!_bPressed || !(bBeginDragFromCapturedMove || bCapturePointerOnPress)) {
            return false;
        }
        if (WidgetTree* tree = owner.getTree()) {
            if (tree->isDragging()) {
                return false;
            }
            const float dist = glm::length(ctx.logicalPoint - _pressPoint);
            if (dist <= dragThreshold) {
                return false;
            }
            if (bCapturePointerOnPress) {
                tree->releasePointerCapture(&owner);
            }
            _bPressed = false;
            if (setPressedState) {
                setPressedState(owner, false);
            }
            if (operationFactory) {
                if (UIDragDropOperationRef op = operationFactory(owner)) {
                    tree->beginDrag(&owner, std::move(op));
                    return true;
                }
            }
        }
        return false;
    case EEvent::MouseButtonReleased:
        releasePress(owner);
        return true;
    default:
        return false;
    }
}

UIDragDropOperationRef UIDragSourceBehavior::onDragDetected(UIElement& owner, const FDragDetectedEvent& event)
{
    (void)event;
    releasePress(owner);
    return operationFactory ? operationFactory(owner) : nullptr;
}

void UIDragSourceBehavior::onDetached(UIElement& owner)
{
    releasePress(owner);
    if (onOwnerDetached) {
        onOwnerDetached(owner);
    }
    UIBehavior::onDetached(owner);
}

void UIDragSourceBehavior::releasePress(UIElement& owner)
{
    _bPressed = false;
    if (setPressedState) {
        setPressedState(owner, false);
    }
    if (bCapturePointerOnPress) {
        if (WidgetTree* tree = owner.getTree()) {
            tree->releasePointerCapture(&owner);
        }
    }
}

bool UIDropTargetBehavior::canAcceptDrop(UIElement& owner, const UIDragDropOperation& operation, const glm::vec2& logicalPoint) const
{
    return canAccept && canAccept(owner, operation, logicalPoint);
}

bool UIDropTargetBehavior::canPreviewDrop(UIElement& owner, const UIDragDropOperation& operation, const glm::vec2& logicalPoint) const
{
    if (canPreview) {
        return canPreview(owner, operation, logicalPoint);
    }
    return canAcceptDrop(owner, operation, logicalPoint);
}

void UIDropTargetBehavior::onDetached(UIElement& owner)
{
    if (setHighlightState) {
        setHighlightState(owner, false);
    }
    if (onOwnerDetached) {
        onOwnerDetached(owner);
    }
    UIBehavior::onDetached(owner);
}

bool acceptsDrop(UIElement& target, const UIDragDropOperation& operation, const glm::vec2& logicalPoint)
{
    const UIDropTargetBehavior* drop = target.findBehavior<UIDropTargetBehavior>();
    return drop && drop->canAcceptDrop(target, operation, logicalPoint);
}

bool previewsDrop(UIElement& target, const UIDragDropOperation& operation, const glm::vec2& logicalPoint)
{
    const UIDropTargetBehavior* drop = target.findBehavior<UIDropTargetBehavior>();
    return drop && drop->canPreviewDrop(target, operation, logicalPoint);
}

void dropOnto(UIElement& target, const UIDragDropOperation& operation, const glm::vec2& logicalPoint)
{
    const UIDropTargetBehavior* drop = target.findBehavior<UIDropTargetBehavior>();
    if (!drop || !drop->canAcceptDrop(target, operation, logicalPoint)) {
        return;
    }
    if (drop->setHighlightState) {
        drop->setHighlightState(target, false);
    }
    if (drop->handleDrop) {
        drop->handleDrop(target, operation, logicalPoint);
    }
}

void highlightDrop(UIElement& target, bool bHighlight)
{
    if (const UIDropTargetBehavior* drop = target.findBehavior<UIDropTargetBehavior>(); drop && drop->setHighlightState) {
        drop->setHighlightState(target, bHighlight);
    }
}

void hoverDrop(UIElement& target, const UIDragDropOperation& operation, const glm::vec2& logicalPoint)
{
    const UIDropTargetBehavior* drop = target.findBehavior<UIDropTargetBehavior>();
    if (drop && drop->updateHover && drop->canPreviewDrop(target, operation, logicalPoint)) {
        drop->updateHover(target, operation, logicalPoint);
    }
}

UIDragDropOperationRef detectDrag(UIElement& source, const FDragDetectedEvent& event)
{
    UIDragSourceBehavior* drag = source.findBehavior<UIDragSourceBehavior>();
    return drag ? drag->onDragDetected(source, event) : nullptr;
}

} // namespace ya
