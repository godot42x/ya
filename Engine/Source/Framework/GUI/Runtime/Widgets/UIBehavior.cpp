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

void UIBehavior::tick(UIElement& owner, float deltaSeconds)
{
    (void)owner;
    (void)deltaSeconds;
}

bool UIBehavior::previewInputEvent(UIElement& owner, const Event& event, const WidgetEventContext& ctx)
{
    (void)owner;
    (void)event;
    (void)ctx;
    return false;
}

bool UIBehavior::handleInputEvent(UIElement& owner, const Event& event, const WidgetEventContext& ctx)
{
    (void)owner;
    (void)event;
    (void)ctx;
    return false;
}

bool UIBehavior::bubbleInputEvent(UIElement& owner, const Event& event, const WidgetEventContext& ctx)
{
    (void)owner;
    (void)event;
    (void)ctx;
    return false;
}

bool UIBehavior::canAcceptDrop(UIElement& owner, const UIDragDropOperation& operation, const glm::vec2& logicalPoint)
{
    (void)owner; (void)operation; (void)logicalPoint; return false;
}

void UIBehavior::onDrop(UIElement& owner, const UIDragDropOperation& operation, const glm::vec2& logicalPoint)
{
    (void)owner; (void)operation; (void)logicalPoint;
}

void UIBehavior::setDropHighlight(UIElement& owner, bool bHighlight)
{
    (void)owner; (void)bHighlight;
}

void UIBehavior::updateDropHover(UIElement& owner, const UIDragDropOperation& operation, const glm::vec2& logicalPoint)
{
    (void)owner; (void)operation; (void)logicalPoint;
}

UIDragDropOperationRef UIBehavior::onDragDetected(UIElement& owner, const FDragDetectedEvent& event)
{
    (void)owner; (void)event; return nullptr;
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
        if (!_bPressed || !bBeginDragFromCapturedMove) {
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
        _bPressed = false;
        if (setPressedState) {
            setPressedState(owner, false);
        }
        if (bCapturePointerOnPress) {
            if (WidgetTree* tree = owner.getTree()) {
                tree->releasePointerCapture(&owner);
            }
        }
        return true;
    default:
        return false;
    }
}

UIDragDropOperationRef UIDragSourceBehavior::onDragDetected(UIElement& owner, const FDragDetectedEvent& event)
{
    (void)event;
    _bPressed = false;
    if (setPressedState) {
        setPressedState(owner, false);
    }
    if (bCapturePointerOnPress) {
        if (WidgetTree* tree = owner.getTree()) {
            tree->releasePointerCapture(&owner);
        }
    }
    return operationFactory ? operationFactory(owner) : nullptr;
}

void UIDragSourceBehavior::onDetached(UIElement& owner)
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
    UIBehavior::onDetached(owner);
}

bool UIDropTargetBehavior::canAcceptDrop(UIElement& owner, const UIDragDropOperation& operation, const glm::vec2& logicalPoint)
{
    return canAccept ? canAccept(owner, operation, logicalPoint) : false;
}

void UIDropTargetBehavior::onDrop(UIElement& owner, const UIDragDropOperation& operation, const glm::vec2& logicalPoint)
{
    setDropHighlight(owner, false);
    if (handleDrop) {
        handleDrop(owner, operation, logicalPoint);
    }
}

void UIDropTargetBehavior::setDropHighlight(UIElement& owner, bool bHighlight)
{
    if (setHighlightState) {
        setHighlightState(owner, bHighlight);
    }
}

void UIDropTargetBehavior::updateDropHover(UIElement& owner, const UIDragDropOperation& operation, const glm::vec2& logicalPoint)
{
    if (updateHover) {
        updateHover(owner, operation, logicalPoint);
    }
}

void UIDropTargetBehavior::onDetached(UIElement& owner)
{
    setDropHighlight(owner, false);
    UIBehavior::onDetached(owner);
}

} // namespace ya
