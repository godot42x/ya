#pragma once

#include "Core/Common/Types.h"

#include <memory>

namespace ya
{

struct UIElement;
struct UIDragDropOperation;
using UIDragDropOperationRef = std::shared_ptr<UIDragDropOperation>;
struct FDragDetectedEvent;
struct WidgetEventContext;
class Event;

struct YA_GUI_API UIBehavior
{
    virtual ~UIBehavior() = default;

    [[nodiscard]] UIElement* getOwner() const { return _owner; }

    virtual void onAttached(UIElement& owner);
    virtual void onDetached(UIElement& owner);
    [[nodiscard]] virtual bool wantsTick() const { return false; }
    virtual void tick(UIElement& owner, float deltaSeconds);

    virtual bool previewInputEvent(UIElement& owner, const Event& event, const WidgetEventContext& ctx);
    virtual bool handleInputEvent(UIElement& owner, const Event& event, const WidgetEventContext& ctx);
    virtual bool bubbleInputEvent(UIElement& owner, const Event& event, const WidgetEventContext& ctx);

    virtual bool canAcceptDrop(UIElement& owner, const std::string& payload, const glm::vec2& logicalPoint);
    virtual bool canAcceptDrop(UIElement& owner, const UIDragDropOperation& operation, const glm::vec2& logicalPoint);
    virtual void onDrop(UIElement& owner, const std::string& payload, const glm::vec2& logicalPoint);
    virtual void onDrop(UIElement& owner, const UIDragDropOperation& operation, const glm::vec2& logicalPoint);
    virtual void setDropHighlight(UIElement& owner, bool bHighlight);
    virtual void updateDropHover(UIElement& owner, const std::string& payload, const glm::vec2& logicalPoint);
    virtual void updateDropHover(UIElement& owner, const UIDragDropOperation& operation, const glm::vec2& logicalPoint);
    virtual UIDragDropOperationRef onDragDetected(UIElement& owner, const FDragDetectedEvent& event);

  protected:
    void invalidateOwnerPaint() const;
    void invalidateOwnerLayout() const;
    void invalidateOwnerSubtree() const;

  private:
    friend struct UIElement;
    UIElement* _owner = nullptr;
};

using UIBehaviorRef = std::shared_ptr<UIBehavior>;

} // namespace ya
