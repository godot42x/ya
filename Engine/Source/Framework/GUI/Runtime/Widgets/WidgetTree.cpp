#include "GUI/Widgets/WidgetTree.h"

#include "Core/Log.h"

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
        _layers[i]->_parent = _root.get();
        _root->_children.push_back(_layers[i]);
    }
}

UIElement* WidgetTree::topmostHitSubtree(UIElement* element, const glm::vec2& logicalPoint)
{
    if (!element->isHitTestableSubtree()) {
        return nullptr;
    }
    const auto children = element->getChildrenInPaintOrder();
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        if (UIElement* hit = topmostHitSubtree(*it, logicalPoint)) {
            return hit;
        }
    }
    if (element->isHitTestableSelf() && element->hitTestLayoutRect(logicalPoint)) {
        return element;
    }
    return nullptr;
}

EWidgetRouteResult WidgetTree::dispatchSubtree(UIElement* element,
                                               const Event& event,
                                               const WidgetEventContext& ctx)
{
    if (!element->isHitTestableSubtree()) {
        return EWidgetRouteResult::NotHandled;
    }

    EWidgetRouteResult result = EWidgetRouteResult::NotHandled;
    const auto         children = element->getChildrenInPaintOrder();
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        const EWidgetRouteResult childResult = dispatchSubtree(*it, event, ctx);
        if (childResult == EWidgetRouteResult::HandledExclusive) {
            return EWidgetRouteResult::HandledExclusive;
        }
        if (childResult == EWidgetRouteResult::HandledPass) {
            result = EWidgetRouteResult::HandledPass;
        }
    }

    if (!element->isHitTestableSelf() ||
        !element->hitTestLayoutRect(ctx.logicalPoint) ||
        !element->handleInputEvent(event, ctx)) {
        return result;
    }
    return element->_hitFilter == EWidgetHitFilter::Stop ? EWidgetRouteResult::HandledExclusive
                                                        : EWidgetRouteResult::HandledPass;
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
    widget->_parent = &parent;
    parent._children.push_back(widget);
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
                auto& siblings = oldParent->_children;
                std::erase_if(siblings, [&](const UIElementRef& ref) { return ref.get() == widget.get(); });
            }
            widget->_parent = nullptr;
        }
    }

    markSubtreeMembership(widget.get(), this);
    widget->_parent = &newParent;
    newParent._children.push_back(widget);
    invalidateLayout();
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
        auto& siblings = oldParent->_children;
        std::erase_if(siblings, [&](const UIElementRef& ref) { return ref.get() == &widget; });
    }
    widget._parent = nullptr;

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

    // Keyboard events route to the focused widget (if any).
    if (eventType == EEvent::KeyPressed || eventType == EEvent::KeyReleased || eventType == EEvent::KeyTyped) {
        if (_focused && _focused->isAttached()) {
            return _focused->handleInputEvent(event, ctx) ? EWidgetRouteResult::HandledExclusive
                                                          : EWidgetRouteResult::NotHandled;
        }
        return EWidgetRouteResult::NotHandled;
    }

    if (eventType != EEvent::MouseButtonPressed &&
        eventType != EEvent::MouseButtonReleased &&
        eventType != EEvent::MouseMoved) {
        return EWidgetRouteResult::NotHandled;
    }

    // Pointer capture overrides the hit walk.
    if (_captured) {
        if (!_captured->isAttached()) {
            _captured = nullptr;
        }
        else {
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
            const bool bHandled = _captured->handleInputEvent(event, captureCtx);
            return bHandled ? (_captured->_hitFilter == EWidgetHitFilter::Stop
                                   ? EWidgetRouteResult::HandledExclusive
                                   : EWidgetRouteResult::HandledPass)
                            : EWidgetRouteResult::NotHandled;
        }
    }

    // Hover tracking: refresh the hovered widget on mouse moves.
    if (eventType == EEvent::MouseMoved) {
        UIElement* newHovered = topmostHit(ctx.logicalPoint);
        if (_hovered != newHovered) {
            if (_hovered && _hovered->isAttached()) {
                _hovered->resetHoverState();
            }
            _hovered = newHovered;
        }
    }

    return dispatchSubtree(_root.get(), event, ctx);
}

// === Focus / capture / hover ===

void WidgetTree::setFocus(UIElement* widget)
{
    if (widget && !widget->isAttached()) {
        YA_CORE_WARN("WidgetTree::setFocus: widget '{}' is not attached to this tree",
                     widget->_name);
        return;
    }
    _focused = widget;
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
    // Clear focus/capture/hover pointing anywhere inside the subtree.
    std::vector<UIElement*> pending{&widget};
    while (!pending.empty()) {
        UIElement* node = pending.back();
        pending.pop_back();
        if (_focused == node) {
            _focused = nullptr;
        }
        if (_captured == node) {
            _captured = nullptr;
        }
        if (_hovered == node) {
            _hovered = nullptr;
        }
        for (const auto& child : node->_children) {
            pending.push_back(child.get());
        }
    }
}

void WidgetTree::onWidgetDetached(UIElement& widget)
{
    clearTransientState(widget);
}

UIElement* WidgetTree::topmostHit(const glm::vec2& logicalPoint) const
{
    return topmostHitSubtree(_root.get(), logicalPoint);
}

} // namespace ya
