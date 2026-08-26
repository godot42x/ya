#include "GUI/Widgets/UIBehavior.h"

#include "GUI/Widgets/UIElement.h"
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

bool UIBehavior::canAcceptDrop(UIElement& owner, const std::string& payload, const glm::vec2& logicalPoint)
{
    (void)owner; (void)payload; (void)logicalPoint; return false;
}

bool UIBehavior::canAcceptDrop(UIElement& owner, const UIDragDropOperation& operation, const glm::vec2& logicalPoint)
{
    return canAcceptDrop(owner, operation.payload, logicalPoint);
}

void UIBehavior::onDrop(UIElement& owner, const std::string& payload, const glm::vec2& logicalPoint)
{
    (void)owner; (void)payload; (void)logicalPoint;
}

void UIBehavior::onDrop(UIElement& owner, const UIDragDropOperation& operation, const glm::vec2& logicalPoint)
{
    onDrop(owner, operation.payload, logicalPoint);
}

void UIBehavior::setDropHighlight(UIElement& owner, bool bHighlight)
{
    (void)owner; (void)bHighlight;
}

void UIBehavior::updateDropHover(UIElement& owner, const std::string& payload, const glm::vec2& logicalPoint)
{
    (void)owner; (void)payload; (void)logicalPoint;
}

void UIBehavior::updateDropHover(UIElement& owner, const UIDragDropOperation& operation, const glm::vec2& logicalPoint)
{
    updateDropHover(owner, operation.payload, logicalPoint);
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

} // namespace ya
