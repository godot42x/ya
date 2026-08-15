#include "GUI/Widgets/Controls/SplitPane.h"

#include "Core/Log.h"
#include "GUI/Widgets/Reactive.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"

namespace ya
{

namespace
{

bool pointInRect(const glm::vec2& point, const Rect2D& rect)
{
    return point.x >= rect.pos.x && point.x <= rect.pos.x + rect.extent.x &&
           point.y >= rect.pos.y && point.y <= rect.pos.y + rect.extent.y;
}

} // namespace

void UISplitPane::layout(const Rect2D& parentRect)
{
    layoutAssigned(computeAnchorRect(parentRect));
}

void UISplitPane::layoutAssigned(const Rect2D& rect)
{
    setLayoutRect(rect);
    if (getChildren().size() > 2) {
        YA_CORE_WARN("UISplitPane '{}': UISplitLayout only arranges the first two children ({} attached)",
                     _name, getChildren().size());
    }
    if (_splitRatioBinding) {
        // Pull the bound ratio into the layout before arranging. value() reads
        // without recording (the dependency was registered at bind time).
        const float ratio = _splitRatioBinding->value();
        if (ratio != _splitLayout.getSplitRatio()) {
            _splitLayout.setSplitRatio(ratio);
        }
    }
    _splitLayout.arrange(*this, _layoutRect);
}

void UISplitPane::bindSplitRatio(std::shared_ptr<Reactive<float>> ref)
{
    // Unbind the previous persistent edge before rebinding, so the old ref no
    // longer holds this widget as a dependent (rebind cleanup).
    if (_splitRatioBinding) {
        _splitRatioBinding->removePersistentDependent(this);
        untrackDependency(_splitRatioBinding.get());
    }
    _splitRatioBinding = std::move(ref);
    if (_splitRatioBinding) {
        _splitRatioBinding->addPersistentDependent(this, ReactiveBase::EDirtyLevel::Layout);
        trackPersistentDependency(_splitRatioBinding.get());
    }
}

void UISplitPane::paint(UIFrameBuilder& builder)
{
    if (!isVisibleForRender()) {
        return;
    }
    builder.countWidget();
    pushPaintWidget(this);
    paintSelf(builder);
    // Each pane is its own clip region: UISplitLayout assigned every arranged
    // child its pane rect as the child's layout rect, so clip each child to
    // that rect. Without this a centered/overflowing child (e.g. text wider
    // than a shrunken pane) bleeds across the divider or outside the split.
    for (UIElement* child : getChildrenInPaintOrder()) {
        builder.pushClip(child->_layoutRect);
        child->paint(builder);
        builder.popClip();
    }
    popPaintWidget();
}

void UISplitPane::paintSelf(UIFrameBuilder& builder)
{
    const glm::vec4 color = _bDraggingDivider
                                ? _dividerDraggingColor
                                : (_bHoveredDivider ? _dividerHoveredColor : _dividerColor);
    builder.addSprite(_splitLayout.getDividerRect(), color, nullptr);
}

bool UISplitPane::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    const EEvent::T eventType = event.getEventType();

    if (eventType == EEvent::MouseMoved && !_bDraggingDivider) {
        _bHoveredDivider = pointInRect(ctx.logicalPoint, _splitLayout.getDividerRect());
    }

    if (eventType == EEvent::MouseButtonPressed) {
        if (!pointInRect(ctx.logicalPoint, _splitLayout.getDividerRect())) {
            return false;
        }
        _bDraggingDivider = true;
        _dragStartRatio   = _splitLayout.getSplitRatio();
        _dragStartPointer = _splitLayout.axisCoordinate(ctx.logicalPoint);
        if (WidgetTree* tree = getTree()) {
            tree->setFocus(this);
            tree->setPointerCapture(this);
        }
        return true;
    }

    if (!ctx.bViaCapture) {
        return false;
    }

    if (eventType == EEvent::MouseMoved && _bDraggingDivider) {
        const float contentExtent = _splitLayout.getOrientation() == ESplitOrientation::Vertical
                                        ? _splitLayout.getContentRect().extent.x
                                        : _splitLayout.getContentRect().extent.y;
        if (contentExtent > 0.0f) {
            _splitLayout.setSplitRatio(_dragStartRatio +
                                       (_splitLayout.axisCoordinate(ctx.logicalPoint) - _dragStartPointer) /
                                           contentExtent);
        }
        return true;
    }

    if (eventType == EEvent::MouseButtonReleased && _bDraggingDivider) {
        _bDraggingDivider = false;
        if (WidgetTree* tree = getTree()) {
            tree->releasePointerCapture(this);
        }
        return true;
    }

    return false;
}

bool UISplitPane::hitTestSelf(const glm::vec2& logicalPoint) const
{
    // Only the divider strip is part of this widget's own hit region; the two
    // panes belong to their children and padding falls through to lower
    // siblings. This keeps the split's full-area rect from stealing hover or
    // the resize cursor from an overlapping interactive child.
    return isHitTestableSelf() && pointInRect(logicalPoint, _splitLayout.getDividerRect());
}

void UISplitPane::clearTransientInputState()
{
    _bDraggingDivider = false;
    _bHoveredDivider  = false;
}

glm::vec2 UISplitPane::computeDesiredSize() const
{
    return _splitLayout.measure(*this);
}

} // namespace ya
