#pragma once

#include "Core/Common/Types.h"
#include "GUI/Widgets/DragDropOperation.h"

#include <functional>
#include <memory>

namespace ya
{

struct UIElement;
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

    virtual bool canAcceptDrop(UIElement& owner, const UIDragDropOperation& operation, const glm::vec2& logicalPoint);
    /// Hover preview may be shown before the point is a valid drop (dock
    /// chooser). Defaults to canAcceptDrop.
    virtual bool canPreviewDrop(UIElement& owner, const UIDragDropOperation& operation, const glm::vec2& logicalPoint);
    virtual void onDrop(UIElement& owner, const UIDragDropOperation& operation, const glm::vec2& logicalPoint);
    virtual void setDropHighlight(UIElement& owner, bool bHighlight);
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

struct YA_GUI_API UIDragSourceBehavior : public UIBehavior
{
    std::function<UIDragDropOperationRef(UIElement& owner)> operationFactory;
    std::function<void(UIElement& owner, bool bPressed)> setPressedState;
    bool  bCapturePointerOnPress = false;
    /// When true, captured mouse moves past `dragThreshold` start a drag
    /// session. Capture without this flag (or `bCapturePointerOnPress`) used
    /// to swallow moves and block WidgetTree::onDragDetected.
    bool  bBeginDragFromCapturedMove = false;
    float dragThreshold = 6.0f;

    bool handleInputEvent(UIElement& owner, const Event& event, const WidgetEventContext& ctx) override;
    UIDragDropOperationRef onDragDetected(UIElement& owner, const FDragDetectedEvent& event) override;
    void onDetached(UIElement& owner) override;

  private:
    bool      _bPressed = false;
    glm::vec2 _pressPoint{0.0f, 0.0f};
};

struct YA_GUI_API UIDropTargetBehavior : public UIBehavior
{
    std::function<bool(UIElement& owner, const UIDragDropOperation& operation, const glm::vec2& logicalPoint)> canAccept;
    std::function<bool(UIElement& owner, const UIDragDropOperation& operation, const glm::vec2& logicalPoint)> canPreview;
    std::function<void(UIElement& owner, const UIDragDropOperation& operation, const glm::vec2& logicalPoint)> handleDrop;
    std::function<void(UIElement& owner, bool bHighlight)> setHighlightState;
    std::function<void(UIElement& owner, const UIDragDropOperation& operation, const glm::vec2& logicalPoint)> updateHover;

    bool canAcceptDrop(UIElement& owner, const UIDragDropOperation& operation, const glm::vec2& logicalPoint) override;
    bool canPreviewDrop(UIElement& owner, const UIDragDropOperation& operation, const glm::vec2& logicalPoint) override;
    void onDrop(UIElement& owner, const UIDragDropOperation& operation, const glm::vec2& logicalPoint) override;
    void setDropHighlight(UIElement& owner, bool bHighlight) override;
    void updateDropHover(UIElement& owner, const UIDragDropOperation& operation, const glm::vec2& logicalPoint) override;
    void onDetached(UIElement& owner) override;
};

using UIBehaviorRef = std::shared_ptr<UIBehavior>;

} // namespace ya
