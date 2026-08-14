#include "GUI/Widgets/WidgetTree.h"

#include "Core/Log.h"

#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/PopupOverlay.h"
#include "GUI/Widgets/Controls/Text.h"

#include <algorithm>

namespace ya
{

namespace
{

constexpr float kCanvasMinSize = 1.0f;

/// Make an element fill its parent rect (stretch anchors, no offset).
UIElementRef makeFillElement(std::string name)
{
    auto element = std::make_shared<UIElement>(std::move(name));
    element->_anchorMin = {0.0f, 0.0f};
    element->_anchorMax = {1.0f, 1.0f};
    // Structural containers (root/layers) are not hit targets themselves;
    // their children are (HitTestInvisible semantics).
    element->_visibility = EWidgetVisibility::HitTestInvisible;
    return element;
}

bool isDescendantOf(const UIElement* candidate, const UIElement* ancestor)
{
    for (const UIElement* node = candidate; node != nullptr; node = node->getParent()) {
        if (node == ancestor) {
            return true;
        }
    }
    return false;
}

} // namespace

WidgetTree::WidgetTree(Extent2D logicalExtent) : _logicalExtent(logicalExtent)
{
    _root = makeFillElement("TreeRoot");
    for (size_t i = 0; i < _layers.size(); ++i) {
        const auto layer = static_cast<ELayer>(i);
        _layers[i]       = makeFillElement("Layer_" + std::to_string(i));
        _layers[i]->_zOrder = static_cast<int>(i);
        _layers[i]->_tree   = this;
        _root->appendChildEdge(_layers[i]);
    }
}

void WidgetTree::collectHitTargetsSubtree(UIElement* element,
                                          const glm::vec2& logicalPoint,
                                          std::vector<UIElement*>& outTargets)
{
    if (!element->isHitTestableSubtree()) {
        return;
    }
    // Clipped containers (scroll viewports): children outside the container
    // rect are not hittable, even though their own layout rects extend past it.
    if (element->cullsChildHits(logicalPoint)) {
        if (element->isHitTestableSelf() && element->hitTestLayoutRect(logicalPoint)) {
            outTargets.push_back(element);
        }
        return;
    }

    const size_t targetCountBeforeChildren = outTargets.size();
    const auto children = element->getChildrenInPaintOrder();
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        collectHitTargetsSubtree(*it, logicalPoint, outTargets);
    }
    if (outTargets.size() == targetCountBeforeChildren &&
        element->isHitTestableSelf() && element->hitTestLayoutRect(logicalPoint)) {
        outTargets.push_back(element);
    }
}

void WidgetTree::markSubtreeMembership(UIElement* widget, WidgetTree* tree)
{
    std::vector<UIElement*> pending{widget};
    while (!pending.empty()) {
        UIElement* node = pending.back();
        pending.pop_back();
        node->_tree = tree;
        for (const auto& child : node->_children) {
            pending.push_back(child.get());
        }
    }
}

namespace
{

void collectFocusablesSubtree(UIElement* element, std::vector<UIElement*>& outFocusables)
{
    if (element->_focusPolicy == EWidgetFocusPolicy::Focusable && element->isVisibleInTree()) {
        outFocusables.push_back(element);
    }
    for (UIElement* child : element->getChildrenInPaintOrder()) {
        collectFocusablesSubtree(child, outFocusables);
    }
}

} // namespace

void WidgetTree::collectFocusables(std::vector<UIElement*>& outFocusables) const
{
    outFocusables.clear();
    for (const auto& layer : _layers) {
        collectFocusablesSubtree(layer.get(), outFocusables);
    }
}

WidgetTree::~WidgetTree()
{
    // Recursively tear down membership for every element (layers and their
    // whole subtree) before the strong refs are released: a widget destroyed
    // after the tree must never still point into the dying tree.
    clearTransientState(*_root);
    std::vector<UIElement*> pending;
    for (auto& layer : _layers) {
        pending.push_back(layer.get());
    }
    while (!pending.empty()) {
        UIElement* node = pending.back();
        pending.pop_back();
        node->_tree   = nullptr;
        node->_parent = nullptr;
        for (const auto& child : node->_children) {
            pending.push_back(child.get());
        }
    }
    _root->_tree   = nullptr;
    _root->_parent = nullptr;
    _root->_children.clear();
    _root->_childSlots.clear();
}

void WidgetTree::setLogicalExtent(Extent2D extent)
{
    _logicalExtent = extent;
    invalidateLayout();
}

UIElement* WidgetTree::getLayer(ELayer layer) const
{
    return _layers[static_cast<size_t>(layer)].get();
}

// === Attach / reparent / detach ===

WidgetAttachment WidgetTree::attach(UIElement& parent, const UIElementRef& widget)
{
    if (!widget) {
        YA_CORE_ERROR("WidgetTree::attach: null widget");
        return {};
    }
    if (widget.get() == &parent) {
        YA_CORE_ERROR("WidgetTree::attach: cannot attach a widget to itself");
        return {};
    }
    if (!contains(parent)) {
        YA_CORE_ERROR("WidgetTree::attach: parent '{}' does not belong to this tree", parent._name);
        return {};
    }
    if (widget->isAttached()) {
        YA_CORE_ERROR("WidgetTree::attach: widget '{}' is already attached; use reparent() for an explicit move",
                      widget->_name);
        return {};
    }
    if (isDescendantOf(&parent, widget.get())) {
        YA_CORE_ERROR("WidgetTree::attach: cannot attach '{}' under its own descendant '{}'",
                      widget->_name, parent._name);
        return {};
    }

    markSubtreeMembership(widget.get(), this);
    parent.appendChildEdge(widget);
    invalidateLayout();
    return WidgetAttachment{.tree = this, .widget = widget};
}

WidgetAttachment WidgetTree::attachToLayer(ELayer layer, const UIElementRef& widget)
{
    return attach(*getLayer(layer), widget);
}

void WidgetTree::reparent(UIElement& newParent, const UIElementRef& widget)
{
    if (!widget) {
        YA_CORE_ERROR("WidgetTree::reparent: null widget");
        return;
    }
    if (widget.get() == &newParent) {
        YA_CORE_ERROR("WidgetTree::reparent: cannot reparent a widget to itself");
        return;
    }
    if (!contains(newParent)) {
        YA_CORE_ERROR("WidgetTree::reparent: new parent '{}' does not belong to this tree", newParent._name);
        return;
    }
    if (isDescendantOf(&newParent, widget.get())) {
        YA_CORE_ERROR("WidgetTree::reparent: cannot reparent '{}' under its own descendant '{}'",
                      widget->_name, newParent._name);
        return;
    }

    if (widget->isAttached()) {
        if (widget->_tree != this) {
            // Explicit cross-tree move: detach from the old tree first.
            widget->_tree->detach(*widget);
        }
        else {
            // Within this tree: unlink from the current parent.
            UIElement* oldParent = widget->_parent;
            if (oldParent) {
                oldParent->removeChildEdge(*widget);
            }
        }
    }

    markSubtreeMembership(widget.get(), this);
    newParent.appendChildEdge(widget);
    invalidateLayout();
}

void WidgetTree::reparentRelativeTo(WidgetTree& tree, UIElement& sibling, const UIElementRef& widget, bool bAfter)
{
    if (!widget || widget.get() == &sibling) {
        return;
    }
    if (!tree.contains(sibling)) {
        YA_CORE_ERROR("WidgetTree::reparentRelativeTo: sibling '{}' does not belong to this tree",
                      sibling._name);
        return;
    }
    UIElement* parent = sibling._parent;
    if (!parent) {
        YA_CORE_ERROR("WidgetTree::reparentRelativeTo: sibling '{}' has no parent", sibling._name);
        return;
    }

    // Cross-tree move: detach from the old tree first.
    if (widget->isAttached() && widget->_tree != &tree) {
        widget->_tree->detach(*widget);
    }

    if (widget->isAttached()) {
        if (UIElement* oldParent = widget->_parent) {
            oldParent->removeChildEdge(*widget);
        }
    }

    const auto it = std::find_if(parent->_children.begin(), parent->_children.end(),
                                 [&](const UIElementRef& ref) { return ref.get() == &sibling; });
    if (it == parent->_children.end()) {
        YA_CORE_ERROR("WidgetTree::reparentRelativeTo: sibling '{}' not found in parent", sibling._name);
        return;
    }
    const size_t siblingIndex = static_cast<size_t>(std::distance(parent->_children.begin(), it));
    const size_t insertAt = bAfter ? siblingIndex + 1 : siblingIndex;

    tree.markSubtreeMembership(widget.get(), &tree);
    parent->insertChildEdge(insertAt, widget);
    tree.invalidateLayout();
}

void WidgetTree::reparentBefore(UIElement& sibling, const UIElementRef& widget)
{
    reparentRelativeTo(*this, sibling, widget, /*bAfter=*/false);
}

void WidgetTree::reparentAfter(UIElement& sibling, const UIElementRef& widget)
{
    reparentRelativeTo(*this, sibling, widget, /*bAfter=*/true);
}

void WidgetTree::detach(UIElement& widget)
{

    if (widget._tree != this) {
        YA_CORE_WARN("WidgetTree::detach: widget '{}' is not attached to this tree", widget._name);
        return;
    }
    for (const auto& layer : _layers) {
        if (layer.get() == &widget) {
            YA_CORE_ERROR("WidgetTree::detach: system layers cannot be detached by project code");
            return;
        }
    }

    if (UIElement* oldParent = widget._parent) {
        oldParent->removeChildEdge(widget);
    }

    // Recursively clear tree membership for the whole subtree; internal
    // parent links inside the subtree remain valid (parents own children).
    std::vector<UIElement*> pending{&widget};
    while (!pending.empty()) {
        UIElement* node = pending.back();
        pending.pop_back();
        node->_tree = nullptr;
        for (const auto& child : node->_children) {
            pending.push_back(child.get());
        }
    }

    clearTransientState(widget);
    invalidateLayout();
}

bool WidgetTree::contains(const UIElement& widget) const
{
    return widget._tree == this;
}

// === Frame passes ===

void WidgetTree::invalidateLayout()
{
    _bLayoutDirty = true;
}

void WidgetTree::layout()
{
    const float width  = std::max(static_cast<float>(_logicalExtent.width), kCanvasMinSize);
    const float height = std::max(static_cast<float>(_logicalExtent.height), kCanvasMinSize);
    _root->layout(Rect2D{.pos = {0.0f, 0.0f}, .extent = {width, height}});
    _bLayoutDirty = false;
}

UIFrameSnapshot WidgetTree::buildSnapshot(const UIFrameBuildContext& ctx)
{
    if (_bLayoutDirty) {
        layout();
    }
    UIFrameBuilder builder(ctx);
    _root->paint(builder);
    return builder.build(_logicalExtent);
}

EWidgetRouteResult WidgetTree::dispatchEvent(const Event& event, const WidgetEventContext& ctx)
{
    const EEvent::T eventType = event.getEventType();
    const bool bPointerEvent = eventType == EEvent::MouseButtonPressed ||
                               eventType == EEvent::MouseButtonReleased ||
                               eventType == EEvent::MouseMoved ||
                               eventType == EEvent::MouseScrolled;
    if (bPointerEvent) {
        _pointerState = {
            .logicalPoint = ctx.logicalPoint,
            .bKnown       = true,
        };
    }

    // Tab / Shift+Tab is handled before ordinary key routing: stable
    // paint-order traversal over attached, visible, focusable widgets with
    // wrap-around. Repeats are ignored so holding Tab does not spin focus.
    if (eventType == EEvent::KeyPressed) {
        const auto& keyEvent = static_cast<const KeyPressedEvent&>(event);
        if (keyEvent._keyCode == EKey::Tab && !keyEvent.bRepeat) {
            std::vector<UIElement*> focusables;
            collectFocusables(focusables);
            if (focusables.empty()) {
                return EWidgetRouteResult::NotHandled;
            }

            UIElement* next = nullptr;
            if (_focused && _focused->isAttached() &&
                _focused->_focusPolicy == EWidgetFocusPolicy::Focusable) {
                const auto it = std::find(focusables.begin(), focusables.end(), _focused);
                if (it != focusables.end()) {
                    const size_t index = static_cast<size_t>(std::distance(focusables.begin(), it));
                    const size_t count = focusables.size();
                    const size_t delta = keyEvent.isShiftPressed() ? count - 1 : 1;
                    next = focusables[(index + delta) % count];
                }
            }
            if (!next) {
                // Nothing focused (or focus sits outside the traversal): start
                // from the front / back of the stable order.
                next = keyEvent.isShiftPressed() ? focusables.back() : focusables.front();
            }
            setFocus(next, /*bFromKeyboard=*/true);
            setRouteTrace(EWidgetRoutePolicy::TabTraversal, next);
            return EWidgetRouteResult::HandledExclusive;
        }
    }

    // Escape cancels an active drag session before any key routing.
    if (isDragging() && eventType == EEvent::KeyPressed) {
        const auto& keyEvent = static_cast<const KeyPressedEvent&>(event);
        if (keyEvent._keyCode == EKey::Escape && !keyEvent.bRepeat) {
            cancelDrag();
            setRouteTrace(EWidgetRoutePolicy::DragSession, nullptr);
            return EWidgetRouteResult::HandledExclusive;
        }
    }

    // Keyboard events route to the focused widget (if any).
    if (eventType == EEvent::KeyPressed || eventType == EEvent::KeyReleased || eventType == EEvent::KeyTyped) {
        if (_focused && _focused->isAttached()) {
            return dispatchRoute(_focused, event, ctx, EWidgetRoutePolicy::Focus, /*bAppendTrace=*/false);
        }
        setRouteTrace(EWidgetRoutePolicy::Focus, nullptr);
        return EWidgetRouteResult::NotHandled;
    }

    if (!bPointerEvent) {
        setRouteTrace(EWidgetRoutePolicy::None, nullptr);
        return EWidgetRouteResult::NotHandled;
    }

    // An active drag session owns pointer moves / releases / presses: the
    // ghost follows the pointer, the release delivers the drop, any new
    // press cancels the drag.
    if (isDragging() &&
        (eventType == EEvent::MouseMoved || eventType == EEvent::MouseButtonReleased ||
         eventType == EEvent::MouseButtonPressed)) {
        UIElement* dragRouteTarget = _dragDropTarget;
        switch (eventType) {
        case EEvent::MouseMoved:
            updateDrag(ctx.logicalPoint);
            dragRouteTarget = _dragDropTarget;
            break;
        case EEvent::MouseButtonReleased:
            dragRouteTarget = findDropTarget(ctx.logicalPoint);
            endDrag(ctx.logicalPoint);
            break;
        case EEvent::MouseButtonPressed:
            cancelDrag();
            break;
        default:
            break;
        }
        setRouteTrace(EWidgetRoutePolicy::DragSession, dragRouteTarget);
        return EWidgetRouteResult::HandledExclusive;
    }

    // Pointer capture overrides the hit walk.
    if (_captured) {
        if (!_captured->isAttached()) {
            _captured = nullptr;
        }
        else {
            refreshPointerPath(_captured);
            // Hover follows the captured widget while capture is active.
            if (eventType == EEvent::MouseMoved) {
                UIElement* newHovered = _captured->hitTestLayoutRect(ctx.logicalPoint) ? _captured : nullptr;
                if (_hovered != newHovered) {
                    if (_hovered && _hovered->isAttached()) {
                        _hovered->resetHoverState();
                    }
                    _hovered = newHovered;
                }
            }
            WidgetEventContext captureCtx = ctx;
            captureCtx.bViaCapture         = true;
            return dispatchRoute(_captured, event, captureCtx,
                                 EWidgetRoutePolicy::PointerCapture, /*bAppendTrace=*/false);
        }
    }

    std::vector<UIElement*> pointerTargets;
    collectHitTargetsSubtree(_root.get(), ctx.logicalPoint, pointerTargets);
    UIElement* pointerTarget = pointerTargets.empty() ? nullptr : pointerTargets.front();
    refreshPointerPath(pointerTarget);

    // Hover tracking: refresh the hovered widget on mouse moves.
    if (eventType == EEvent::MouseMoved) {
        UIElement* newHovered = pointerTarget;
        if (_hovered != newHovered) {
            if (_hovered && _hovered->isAttached()) {
                _hovered->resetHoverState();
            }
            _hovered = newHovered;
        }
    }

    // A press moves the interaction to the widget under the pointer even
    // without an intervening move: hover left on a previously hovered widget
    // (e.g. a button clicked just before) must clear when a press lands
    // elsewhere. The pressed widget sets its own hover flag in its press
    // handler; here we only retire the stale one.
    if (eventType == EEvent::MouseButtonPressed) {
        UIElement* newHovered = pointerTarget;
        if (_hovered != newHovered) {
            if (_hovered && _hovered->isAttached()) {
                _hovered->resetHoverState();
            }
            _hovered = newHovered;
        }
    }

    EWidgetRouteResult result = EWidgetRouteResult::NotHandled;
    bool bFirstRoute = true;
    for (UIElement* target : pointerTargets) {
        const EWidgetRoutePolicy policy = classifyPointerRoute(buildPath(target));
        const EWidgetRouteResult routeResult =
            dispatchRoute(target, event, ctx, policy,
                          /*bAppendTrace=*/!bFirstRoute);
        bFirstRoute = false;
        result = mergeRouteResult(result, routeResult);
        if (result == EWidgetRouteResult::HandledExclusive) {
            return result;
        }
    }
    if (pointerTargets.empty()) {
        setRouteTrace(EWidgetRoutePolicy::HitTest, nullptr);
    }
    return result;
}

// === Focus / capture / hover ===

void WidgetTree::setFocus(UIElement* widget, bool bFromKeyboard)
{
    if (widget && !widget->isAttached()) {
        YA_CORE_WARN("WidgetTree::setFocus: widget '{}' is not attached to this tree",
                     widget->_name);
        return;
    }
    if (_focused == widget) {
        return;
    }
    if (_focused && _focused->isAttached()) {
        _focused->onFocusLost();
    }
    _focused = widget;
    if (_focused) {
        _focused->onFocusGained(bFromKeyboard);
    }
    refreshFocusPath();
}

void WidgetTree::setPointerCapture(UIElement* widget)
{
    if (widget && !widget->isAttached()) {
        YA_CORE_WARN("WidgetTree::setPointerCapture: widget '{}' is not attached to this tree",
                     widget->_name);
        return;
    }
    _captured = widget;
}

void WidgetTree::releasePointerCapture(UIElement* widget)
{
    if (_captured == widget) {
        _captured = nullptr;
    }
}

// === Internals ===

void WidgetTree::clearTransientState(UIElement& widget)
{
    // Clear focus/capture/hover pointing anywhere inside the subtree and ask
    // every widget in it to drop its own transient input state (hover /
    // press / drag), so stale state never survives a re-attach.
    std::vector<UIElement*> pending{&widget};
    while (!pending.empty()) {
        UIElement* node = pending.back();
        pending.pop_back();
        if (_focused == node) {
            _focused = nullptr;
            node->onFocusLost();
        }
        if (_captured == node) {
            _captured = nullptr;
        }
        if (_hovered == node) {
            _hovered = nullptr;
        }
        if (std::find(_pointerPath.begin(), _pointerPath.end(), node) != _pointerPath.end()) {
            _pointerPath.clear();
        }
        if (std::find(_focusPath.begin(), _focusPath.end(), node) != _focusPath.end()) {
            _focusPath.clear();
        }
        node->clearTransientInputState();
        for (const auto& child : node->_children) {
            pending.push_back(child.get());
        }
    }
}

void WidgetTree::onWidgetDetached(UIElement& widget)
{
    clearTransientState(widget);
    // A drag session pointing into the detached subtree cannot continue
    // (source or ghost removed): abort it so no stale pointers survive.
    if (isDragging() && (_dragSource == &widget || _dragGhost.get() == &widget)) {
        cancelDrag();
    }
}

UIElement* WidgetTree::topmostHit(const glm::vec2& logicalPoint) const
{
    std::vector<UIElement*> targets;
    collectHitTargetsSubtree(_root.get(), logicalPoint, targets);
    return targets.empty() ? nullptr : targets.front();
}

std::vector<UIElement*> WidgetTree::buildPath(UIElement* target)
{
    std::vector<UIElement*> path;
    for (UIElement* node = target; node != nullptr; node = node->getParent()) {
        path.push_back(node);
    }
    std::reverse(path.begin(), path.end());
    return path;
}

EWidgetRouteResult WidgetTree::mergeRouteResult(EWidgetRouteResult current,
                                                 EWidgetRouteResult next)
{
    if (current == EWidgetRouteResult::HandledExclusive ||
        next == EWidgetRouteResult::HandledExclusive) {
        return EWidgetRouteResult::HandledExclusive;
    }
    if (current == EWidgetRouteResult::HandledPass ||
        next == EWidgetRouteResult::HandledPass) {
        return EWidgetRouteResult::HandledPass;
    }
    return EWidgetRouteResult::NotHandled;
}

EWidgetRoutePolicy WidgetTree::classifyPointerRoute(const std::vector<UIElement*>& path)
{
    for (auto it = path.rbegin(); it != path.rend(); ++it) {
        if (const auto* popup = dynamic_cast<const UIPopupOverlay*>(*it)) {
            return popup->_bModal ? EWidgetRoutePolicy::Modal : EWidgetRoutePolicy::Popup;
        }
    }
    return EWidgetRoutePolicy::HitTest;
}

EWidgetRouteResult WidgetTree::dispatchRoute(UIElement* target,
                                             const Event& event,
                                             const WidgetEventContext& ctx,
                                             EWidgetRoutePolicy policy,
                                             bool bAppendTrace)
{
    const std::vector<UIElement*> path = buildPath(target);
    if (path.empty()) {
        if (!bAppendTrace) {
            beginRouteTrace(policy, nullptr);
        }
        return EWidgetRouteResult::NotHandled;
    }

    // Route callbacks may detach/reparent widgets. Keep every initially
    // resolved node alive for the duration, while checking membership before
    // each delivery so subsequent phases never call a detached widget.
    std::vector<UIElementRef> retainedPath;
    retainedPath.reserve(path.size());
    for (UIElement* node : path) {
        retainedPath.push_back(node->shared_from_this());
    }

    if (!bAppendTrace) {
        beginRouteTrace(policy, target);
    }

    const auto isLiveRouteNode = [this](const UIElement* node) {
        return node == _root.get() || node->getTree() == this;
    };
    const auto invoke = [&](UIElement& node, EWidgetEventRoutePhase phase) {
        if (!isLiveRouteNode(&node)) {
            return EWidgetRouteResult::NotHandled;
        }
        WidgetEventContext routedCtx = ctx;
        routedCtx.phase = phase;
        bool bHandled = false;
        switch (phase) {
        case EWidgetEventRoutePhase::Preview:
            bHandled = node.previewInputEvent(event, routedCtx);
            break;
        case EWidgetEventRoutePhase::Target:
            bHandled = node.handleInputEvent(event, routedCtx);
            break;
        case EWidgetEventRoutePhase::Bubble:
            bHandled = node.bubbleInputEvent(event, routedCtx);
            break;
        }
        appendRouteTraceStep(node, phase, bHandled);
        if (!bHandled) {
            return EWidgetRouteResult::NotHandled;
        }
        // Focus is exclusive by ownership: a focused leaf that accepts a key
        // consumes it regardless of pointer-style Pass/Stop hit filtering.
        if (policy == EWidgetRoutePolicy::Focus && phase == EWidgetEventRoutePhase::Target) {
            return EWidgetRouteResult::HandledExclusive;
        }
        return node._hitFilter == EWidgetHitFilter::Stop
                   ? EWidgetRouteResult::HandledExclusive
                   : EWidgetRouteResult::HandledPass;
    };

    EWidgetRouteResult result = EWidgetRouteResult::NotHandled;
    for (size_t index = 0; index + 1 < path.size(); ++index) {
        result = mergeRouteResult(result, invoke(*path[index], EWidgetEventRoutePhase::Preview));
        if (result == EWidgetRouteResult::HandledExclusive) {
            _lastRouteTrace.result = result;
            return result;
        }
    }

    result = mergeRouteResult(result, invoke(*path.back(), EWidgetEventRoutePhase::Target));
    if (result == EWidgetRouteResult::HandledExclusive) {
        _lastRouteTrace.result = result;
        return result;
    }

    for (size_t index = path.size() - 1; index-- > 0;) {
        result = mergeRouteResult(result, invoke(*path[index], EWidgetEventRoutePhase::Bubble));
        if (result == EWidgetRouteResult::HandledExclusive) {
            _lastRouteTrace.result = result;
            return result;
        }
    }

    _lastRouteTrace.result = result;
    return result;
}

void WidgetTree::refreshPointerPath(UIElement* target)
{
    _pointerPath = target ? buildPath(target) : std::vector<UIElement*>{};
}

void WidgetTree::refreshFocusPath()
{
    _focusPath = _focused ? buildPath(_focused) : std::vector<UIElement*>{};
}

void WidgetTree::beginRouteTrace(EWidgetRoutePolicy policy, UIElement* target)
{
    _lastRouteTrace.policy = policy;
    _lastRouteTrace.target = target ? target->_name : "";
    _lastRouteTrace.path.clear();
    _lastRouteTrace.steps.clear();
    _lastRouteTrace.result = EWidgetRouteResult::NotHandled;
    for (UIElement* node : buildPath(target)) {
        _lastRouteTrace.path.push_back(node->_name);
    }
}

void WidgetTree::appendRouteTraceStep(const UIElement& widget,
                                      EWidgetEventRoutePhase phase,
                                      bool bHandled)
{
    _lastRouteTrace.steps.push_back({
        .widget = widget._name,
        .phase = phase,
        .bHandled = bHandled,
        .hitFilter = widget._hitFilter,
    });
}

// === Drag & drop session ===

void WidgetTree::beginDrag(UIElement* source, std::string payload, std::string ghostLabel)
{
    if (isDragging()) {
        cancelDrag();
    }
    _dragSource  = source;
    _dragPayload = std::move(payload);
    _dragPoint   = {};

    // Ghost on the DragIme layer: visible but never hit-testable.
    auto ghost = std::make_shared<UIPanel>("DragGhost");
    ghost->_color      = {0.24f, 0.46f, 0.82f, 0.75f};
    ghost->_visibility = EWidgetVisibility::SelfHitTestInvisible;
    ghost->_position   = {0.0f, 0.0f};
    ghost->_size       = {160.0f, 24.0f};

    auto label = std::make_shared<UIText>("DragGhostLabel");
    label->_text     = std::move(ghostLabel);
    label->_fontSize = 13;
    label->_color    = {0.95f, 0.96f, 0.98f, 1.0f};
    label->_anchorMin = {0.0f, 0.0f};
    label->_anchorMax = {1.0f, 1.0f};
    label->_size      = {0.0f, 0.0f};
    label->_hAlign    = EWidgetAlignH::Center;
    label->_vAlign    = EWidgetAlignV::Center;
    ghost->addDetachedChild(label);

    attachToLayer(ELayer::DragIme, ghost);
    _dragGhost = ghost;
    invalidateLayout();
}

UIElement* WidgetTree::findDropTarget(const glm::vec2& logicalPoint) const
{
    for (UIElement* node = topmostHit(logicalPoint); node != nullptr; node = node->getParent()) {
        if (node->canAcceptDrop(_dragPayload, logicalPoint)) {
            return node;
        }
    }
    return nullptr;
}

void WidgetTree::updateDrag(const glm::vec2& logicalPoint)
{
    if (!isDragging()) {
        return;
    }
    _dragPoint = logicalPoint;
    if (_dragGhost) {
        _dragGhost->_position = logicalPoint + glm::vec2(10.0f, 10.0f);
        invalidateLayout();
    }

    UIElement* target = findDropTarget(logicalPoint);
    if (target != _dragDropTarget) {
        if (_dragDropTarget) {
            _dragDropTarget->setDropHighlight(false);
        }
        _dragDropTarget = target;
        if (_dragDropTarget) {
            _dragDropTarget->setDropHighlight(true);
        }
    }
}

void WidgetTree::clearDragSession()
{
    if (_dragDropTarget) {
        _dragDropTarget->setDropHighlight(false);
        _dragDropTarget = nullptr;
    }
    _dragPayload.clear();
    _dragSource = nullptr;
    if (_dragGhost && _dragGhost->isAttached()) {
        detach(*_dragGhost); // payload already cleared: no recursive cancel
    }
    _dragGhost.reset();
}

void WidgetTree::endDrag(const glm::vec2& logicalPoint)
{
    if (!isDragging()) {
        return;
    }
    UIElement*       target  = findDropTarget(logicalPoint);
    const std::string payload = _dragPayload;
    clearDragSession();
    if (target) {
        target->onDrop(payload, logicalPoint);
    }
}

void WidgetTree::cancelDrag()
{
    if (!isDragging()) {
        return;
    }
    clearDragSession();
}

} // namespace ya
