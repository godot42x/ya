#include "Render/Resources/FontManager.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GUI/Widgets/DragDropOperation.h"

#include "Core/Event.h"
#include "Core/KeyCode.h"
#include "Core/Log.h"
#include "Core/Profiling/PerfState.h"
#include "Core/Profiling/Profiling.h"

#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Border.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/PopupOverlay.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/GuiFrameInspector.h"
#include "GUI/Widgets/Theme.h"

#include <algorithm>
#include <chrono>

namespace ya
{

UICanvasRoot::UICanvasRoot(std::string name) : UIElement(std::move(name))
{
    installLayout(std::make_unique<UICanvasLayout>());
    setVisibility(EWidgetVisibility::HitTestInvisible);
}

namespace
{

constexpr float kCanvasMinSize = 1.0f;

UIElementRef makeCanvasRoot(std::string name)
{
    return std::make_shared<UICanvasRoot>(std::move(name));
}

UIElementRef makeLayerElement(std::string name)
{
    auto element = std::make_shared<UICanvasRoot>(std::move(name));
    // Structural containers (root/layers) are not hit targets themselves;
    // their children are (HitTestInvisible semantics).
    element->setVisibility(EWidgetVisibility::HitTestInvisible);
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
    _root = makeCanvasRoot("TreeRoot");
    _root->_tree = this;
    for (size_t i = 0; i < _layers.size(); ++i) {
        _layers[i]       = makeLayerElement("Layer_" + std::to_string(i));
        _layers[i]->_zOrder = static_cast<int>(i);
        _root->appendChildEdge(_layers[i]);
        if (UISlot* edge = _root->getSlotForChild(*_layers[i])) {
            FCanvasSlotArgs fillArgs;
            fillArgs.anchorMin = {0.0f, 0.0f};
            fillArgs.anchorMax = {1.0f, 1.0f};
            edge->applyArgs(fillArgs);
        }
    }
}

void WidgetTree::setTheme(UITheme* theme)
{
    if (_theme == theme) {
        return;
    }
    _theme = theme;
    // Bump the generation token: every widget that read it during paint
    // (resolveThemeStyle) is now a dependent and repaints on the next
    // snapshot. == on uint64_t is O(1), so this is the cheap, precise form
    // of "resolve upstream changed".
    _themeGeneration->set(_themeGeneration->value() + 1);
}

void WidgetTree::setTextureSource(IGuiTextureSource* source)
{
    if (_textureSource == source) {
        return;
    }
    _textureSource = source;
    _textureCatalog.setSource(source);
    _textureCatalog.invalidateAll();
    _bHasTextureEpoch = false;
}

UIElement* WidgetTree::hitTestAt(UIElement* element,
                                 const glm::vec2& logicalPoint,
                                 bool bForHover,
                                 UIElement* skipSubtree)
{
    if (skipSubtree) {
        // Skip the dragged source subtree: a floating window that follows
        // the pointer must not shadow the drop targets beneath it. Opt-in
        // only (dock/treeview/selectable containers are their own sources but
        // the pointer never sits between them and their targets).
        for (UIElement* node = element; node != nullptr; node = node->getParent()) {
            if (node == skipSubtree) {
                return nullptr;
            }
        }
    }
    if (!element->isHitTestableSubtree()) {
        return nullptr;
    }
    // Clipped containers (scroll viewports): children outside the container
    // rect are not hittable, even though their own layout rects extend past
    // it. Only the container itself can be hit here.
    if (element->cullsChildHits(logicalPoint)) {
        return element->hitTestSelf(logicalPoint) ? element : nullptr;
    }

    // Children before self, zOrder high first: the topmost descendant wins and
    // the search stops at the first hit.
    const auto children = element->getChildrenInPaintOrder();
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        if (UIElement* hit = hitTestAt(*it, logicalPoint, bForHover, skipSubtree)) {
            return hit;
        }
    }
    if (!element->hitTestSelf(logicalPoint)) {
        return nullptr;
    }
    // A hover-transparent shield (non-modal popup) swallows presses but lets
    // the hover walk continue to a visible sibling beneath it.
    if (bForHover && element->isHoverTransparent()) {
        return nullptr;
    }
    return element;
}

UIElement* WidgetTree::hoverOwnerAlongPath(UIElement* target, const glm::vec2& logicalPoint)
{
    // The target is the single topmost hit. Hover chrome must still match
    // hitTestSelf: an expander is hoverable, but only the header is its
    // hover region, so a body label must not light the header (or an
    // ancestor framed section). A text child still resolves to its button
    // because the button's hitTestSelf is the full layout rect.
    for (UIElement* node = target; node != nullptr; node = node->getParent()) {
        if (node->isHoverable() && node->isAttached() && node->hitTestSelf(logicalPoint)) {
            return node;
        }
    }
    return nullptr;
}

void WidgetTree::updateHovered(UIElement* widget)
{
    if (_hovered == widget) {
        return;
    }
    if (_hovered && _hovered->isAttached()) {
        _hovered->onPointerLeave();
    }
    _hovered = widget;
    if (_hovered && _hovered->isAttached()) {
        _hovered->onPointerEnter();
    }
    // Tooltip dwell restarts on every hover change; a stale tooltip is
    // removed immediately so it never points at the wrong widget.
    _hoveredSinceFrame = _frameCounter;
    removeTooltip();
}

void WidgetTree::clearPointerOverState()
{
    updateHovered(nullptr);
    refreshPointerPath(nullptr);
}

void WidgetTree::removeTooltip()
{
    if (_tooltipHost && _tooltipHost->isAttached()) {
        detach(*_tooltipHost);
    }
    _tooltipHost.reset();
    _bTooltipShown = false;
}

void WidgetTree::updateTooltip()
{
    // Dwell delay: show the tooltip after ~0.5s of uninterrupted hover
    // (frame-count based; no wall clock needed).
    constexpr uint32_t kTooltipDwellFrames = 30;
    if (_bTooltipShown || !_hovered || _hovered->_tooltip.empty() ||
        _frameCounter - _hoveredSinceFrame < kTooltipDwellFrames) {
        return;
    }
    auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, 12);
    const float textW = font ? font->measureText(_hovered->_tooltip) : 80.0f;
    const float lineH = font ? font->lineHeight : 16.0f;
    const glm::vec2 hostSize{textW + 16.0f, lineH + 8.0f};

    auto host = std::make_shared<UIBorder>("TooltipHost");
    host->setStyleKey("tooltip");
    host->setPadding(FMargin{8.0f, 4.0f, 8.0f, 4.0f});

    auto label = std::make_shared<UIText>("TooltipLabel");
    label->_fontSize  = 12;
    label->setText(_hovered->_tooltip);
    host->addDetachedChild(label);

    // Anchor below the hovered widget's rect (clamped into the window).
    const Rect2D& target = _hovered->_layoutRect;
    FCanvasSlotArgs hostArgs;
    hostArgs.offset    = {target.pos.x, target.pos.y + target.extent.y + 4.0f};
    hostArgs.fixedSize = hostSize;
    attachToLayer(ELayer::Tooltip, host, hostArgs);
    _tooltipHost = host;
    _bTooltipShown = true;
}

void WidgetTree::preparePointerState(EEvent::T eventType, const WidgetEventContext& ctx)
{
    const bool bPointerEvent = eventType == EEvent::MouseButtonPressed ||
                               eventType == EEvent::MouseButtonReleased ||
                               eventType == EEvent::MouseMoved ||
                               eventType == EEvent::MouseScrolled;
    if (!bPointerEvent) {
        return;
    }
    _pointerState = {
        .logicalPoint = ctx.logicalPoint,
        .bKnown       = true,
    };
}

EWidgetRouteResult WidgetTree::dispatchCapturedPointerEvent(const Event& event,
                                                            const WidgetEventContext& ctx,
                                                            EEvent::T eventType)
{
    if (!_captured) {
        return EWidgetRouteResult::NotHandled;
    }
    if (!_captured->isAttached()) {
        YA_CORE_WARN("WidgetTree: dropped pointer capture on detached '{}' before dispatch",
                     _captured->_name);
        _captured = nullptr;
        ++_pointerSessionRecoveries;
        return EWidgetRouteResult::NotHandled;
    }

    // A widget may become disabled while it owns pointer capture (for example
    // a presenter flips its enabled gate during an active press session).
    // Disabled subtrees are input-inert, so the normal route executor would
    // drop the release before the widget can clear its transient pressed /
    // capture state. On release, clear that stale session explicitly.
    if (!_captured->isEnabledInTree()) {
        if (eventType == EEvent::MouseButtonReleased) {
            UIElement* captured = _captured;
            _captured = nullptr;
            captured->clearTransientInputState();
        }
        return EWidgetRouteResult::NotHandled;
    }

    refreshPointerPath(_captured);
    if (eventType == EEvent::MouseMoved) {
        UIElement* newHovered = (_captured->isHoverable() &&
                                 _captured->hitTestLayoutRect(ctx.logicalPoint))
                                    ? _captured
                                    : nullptr;
        updateHovered(newHovered);
    }

    WidgetEventContext captureCtx = ctx;
    captureCtx.bViaCapture         = true;
    return dispatchRoute(_captured, event, captureCtx,
                         EWidgetRoutePolicy::PointerCapture, /*bAppendTrace=*/false);
}

void WidgetTree::markSubtreeMembership(UIElement* widget, WidgetTree* tree)
{
    if (widget->_tree) {
        ++widget->_tree->_visibilityRevision;
    }
    if (tree && tree != widget->_tree) {
        ++tree->_visibilityRevision;
    }
    std::vector<UIElement*> pending{widget};
    while (!pending.empty()) {
        UIElement* node = pending.back();
        pending.pop_back();
        node->_tree = tree;
        // A new tree/parent edge may use a different layout contract even if
        // the eventual rect happens to be identical. Never reuse the old
        // assigned-layout proof across attach/reparent boundaries.
        ++node->_layoutRevision;
        node->_assignedLayoutRevision = 0;
        for (const auto& child : node->_children) {
            pending.push_back(child.get());
        }
    }
}

void WidgetTree::prepareSubtree(UIElement* widget)
{
    if (!widget) {
        return;
    }
    widget->prepareForAttach();
    for (const auto& child : widget->_children) {
        prepareSubtree(child.get());
    }
}

void WidgetTree::notifyAttachedSubtree(UIElement* widget)
{
    if (!widget) {
        return;
    }
    for (const UIBehaviorRef& behavior : widget->getBehaviors()) {
        if (behavior) {
            behavior->onAttached(*widget);
        }
    }
    widget->onAttached();
    for (const auto& child : widget->_children) {
        notifyAttachedSubtree(child.get());
    }
}

void WidgetTree::notifyDetachedSubtree(UIElement* widget)
{
    if (!widget) {
        return;
    }
    for (const UIBehaviorRef& behavior : widget->getBehaviors()) {
        if (behavior) {
            behavior->onDetached(*widget);
        }
    }
    widget->onDetached();
    for (const auto& child : widget->_children) {
        notifyDetachedSubtree(child.get());
    }
}

void WidgetTree::tickSubtree(UIElement* widget, float deltaSeconds)
{
    if (!widget || !widget->isVisibleInTree()) {
        return;
    }
    if (widget->wantsTick()) {
        widget->tick(deltaSeconds);
    }
    for (const auto& child : widget->getChildren()) {
        tickSubtree(child.get(), deltaSeconds);
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
        node->_slot   = nullptr;
        for (const auto& child : node->_children) {
            pending.push_back(child.get());
        }
    }
    _root->_tree   = nullptr;
    _root->_parent = nullptr;
    _root->_slot   = nullptr;
    _root->_children.clear();
    _root->_childSlots.clear();
}

void WidgetTree::setLogicalExtent(Extent2D extent)
{
    _logicalExtent = extent;
    invalidateLayout();
}

UICanvasRoot* WidgetTree::getLayer(ELayer layer) const
{
    return static_cast<UICanvasRoot*>(_layers[static_cast<size_t>(layer)].get());
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

    prepareSubtree(widget.get());
    markSubtreeMembership(widget.get(), this);
    parent.appendChildEdge(widget);
    parent.markLayoutDirty(EUIInvalidationReason::ChildStructure);
    notifyAttachedSubtree(widget.get());
    invalidateLayout();
    return WidgetAttachment{.tree = this, .widget = widget};
}

WidgetAttachment WidgetTree::attach(UIElement& parent,
                                    const UIElementRef& widget,
                                    FChildSlotInitializer init)
{
    WidgetAttachment attachment = attach(parent, widget);
    if (attachment.valid() && init) {
        parent.initializeChildSlot(*widget, std::move(init));
    }
    return attachment;
}

WidgetAttachment WidgetTree::attach(UIElement& parent,
                                    const UIElementRef& widget,
                                    const FCanvasSlotArgs& args)
{
    if (!widget) {
        return {};
    }
    WidgetAttachment attachment = attach(parent, widget);
    if (!attachment.valid()) {
        return attachment;
    }
    if (UISlot* edge = parent.getSlotForChild(*widget)) {
        YA_CORE_ASSERT(edge->applyArgs(args),
                       "WidgetTree::attach: parent '{}' is not a canvas host; cannot apply FCanvasSlotArgs to '{}'",
                       parent._name,
                       widget->_name);
    }
    return attachment;
}

WidgetAttachment WidgetTree::attachToLayer(ELayer layer,
                                          const UIElementRef& widget,
                                          const FCanvasSlotArgs& args)
{
    return attach(*getLayer(layer), widget, args);
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

    if (widget->isAttached() && widget->_tree == this && widget->_parent == &newParent) {
        const auto it = std::find_if(newParent._children.begin(), newParent._children.end(),
                                     [&](const UIElementRef& ref) { return ref.get() == widget.get(); });
        const size_t fromIndex =
            it == newParent._children.end() ? newParent._children.size()
                                            : static_cast<size_t>(std::distance(newParent._children.begin(), it));
        if (fromIndex == newParent._children.size()) {
            return;
        }
        UIElementRef            childRef = std::move(newParent._children[fromIndex]);
        std::unique_ptr<UISlot> slot     = std::move(newParent._childSlots[fromIndex]);
        newParent._children.erase(newParent._children.begin() + static_cast<std::ptrdiff_t>(fromIndex));
        newParent._childSlots.erase(newParent._childSlots.begin() + static_cast<std::ptrdiff_t>(fromIndex));
        newParent._children.push_back(std::move(childRef));
        newParent._childSlots.push_back(std::move(slot));
        invalidateLayout();
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

    const bool bWasAttached = widget->isAttached();
    if (!bWasAttached) {
        prepareSubtree(widget.get());
    }
    markSubtreeMembership(widget.get(), this);
    newParent.appendChildEdge(widget);
    newParent.markLayoutDirty(EUIInvalidationReason::ChildStructure);
    if (!bWasAttached) {
        notifyAttachedSubtree(widget.get());
    }
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

    if (widget->isAttached() && widget->_tree == &tree && widget->_parent == parent) {
        const auto fromIt = std::find_if(parent->_children.begin(), parent->_children.end(),
                                         [&](const UIElementRef& ref) { return ref.get() == widget.get(); });
        const auto siblingIt = std::find_if(parent->_children.begin(), parent->_children.end(),
                                            [&](const UIElementRef& ref) { return ref.get() == &sibling; });
        const size_t fromIndex =
            fromIt == parent->_children.end() ? parent->_children.size()
                                              : static_cast<size_t>(std::distance(parent->_children.begin(), fromIt));
        size_t siblingIndex =
            siblingIt == parent->_children.end() ? parent->_children.size()
                                                 : static_cast<size_t>(std::distance(parent->_children.begin(), siblingIt));
        if (fromIndex == parent->_children.size() || siblingIndex == parent->_children.size()) {
            return;
        }
        if (fromIndex < siblingIndex) {
            --siblingIndex;
        }
        const size_t insertAt = bAfter ? siblingIndex + 1 : siblingIndex;
        UIElementRef            childRef = std::move(parent->_children[fromIndex]);
        std::unique_ptr<UISlot> slot     = std::move(parent->_childSlots[fromIndex]);
        parent->_children.erase(parent->_children.begin() + static_cast<std::ptrdiff_t>(fromIndex));
        parent->_childSlots.erase(parent->_childSlots.begin() + static_cast<std::ptrdiff_t>(fromIndex));
        const size_t clampedInsertAt = std::min(insertAt, parent->_children.size());
        parent->_children.insert(parent->_children.begin() + static_cast<std::ptrdiff_t>(clampedInsertAt),
                                 std::move(childRef));
        parent->_childSlots.insert(parent->_childSlots.begin() + static_cast<std::ptrdiff_t>(clampedInsertAt),
                                   std::move(slot));
        tree.invalidateLayout();
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

    const bool bWasAttached = widget->isAttached();
    if (!bWasAttached) {
        tree.prepareSubtree(widget.get());
    }
    tree.markSubtreeMembership(widget.get(), &tree);
    parent->insertChildEdge(insertAt, widget);
    if (!bWasAttached) {
        tree.notifyAttachedSubtree(widget.get());
    }
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
    if (widget._tree != nullptr && widget._tree != this) {
        YA_CORE_WARN("WidgetTree::detach: widget '{}' is not attached to this tree", widget._name);
        return;
    }
    if (widget._tree == nullptr) {
        // Parent still set after a chrome subtree was detached from the tree.
        // Unlink so a later attach/addDetachedChild does not see a stale parent.
        if (UIElement* oldParent = widget._parent) {
            UIElementRef keepAlive;
            for (const auto& ref : oldParent->_children) {
                if (ref.get() == &widget) {
                    keepAlive = ref;
                    break;
                }
            }
            oldParent->removeChildEdge(widget);
        }
        return;
    }

    for (const auto& layer : _layers) {
        if (layer.get() == &widget) {
            // System layers are owned by the tree and cannot be detached by
            // project code. This is a defensive rejection of caller misuse,
            // not a broken invariant -- logging and refusing keeps the layer
            // in the tree (the caller's old reference keeps meaning "the layer")
            // without taking the process down over a recoverable call.
            YA_CORE_WARN("WidgetTree::detach: system layer '{}' cannot be detached by project code", widget._name);
            return;
        }
    }

    // Keep the widget alive through this call: removeChildEdge drops the
    // parent's strong ref, and when that was the last one the widget would
    // be destroyed right here — before membership clearing below — so the
    // destructor's "detached before destroy" assert would fire on freed
    // memory. The caller may also hold only a raw reference.
    UIElementRef keepAlive;
    if (UIElement* oldParent = widget._parent) {
        for (const auto& ref : oldParent->_children) {
            if (ref.get() == &widget) {
                keepAlive = ref;
                break;
            }
        }
        oldParent->removeChildEdge(widget);
    }

    notifyDetachedSubtree(&widget);
    ++_visibilityRevision;
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
    std::vector<UIElement*> cachePending{&widget};
    while (!cachePending.empty()) {
        UIElement* node = cachePending.back();
        cachePending.pop_back();
        for (auto& cache : _itemCache) {
            cache.erase(node->getRuntimeId());
        }
        for (const auto& child : node->_children) {
            cachePending.push_back(child.get());
        }
    }
    invalidateLayout();
}

bool WidgetTree::contains(const UIElement& widget) const
{
    return widget._tree == this;
}

// === Frame passes ===

void WidgetTree::setDpiScale(float scale)
{
    if (scale <= 0.0f) scale = 1.0f;
    _dpiScale = scale;
}

void WidgetTree::publishDpiScale(float scale)
{
    setDpiScale(scale);
    // Same value on both consumers: the tree folds it into target-pixel
    // mapping at buildSnapshot; the font stack rasterizes glyphs at it.
    FontManager::get()->setActiveDpiScale(_dpiScale);
}

void WidgetTree::setClipboardText(std::string text)
{
    if (_clipboardWrite) {
        _clipboardWrite(text);
        return;
    }
    _clipboardText = std::move(text);
}

std::string WidgetTree::getClipboardText() const
{
    if (_clipboardRead) {
        return _clipboardRead();
    }
    return _clipboardText;
}

void WidgetTree::setClipboardHooks(std::function<std::string()> read,
                                   std::function<void(const std::string&)> write)
{
    _clipboardRead  = std::move(read);
    _clipboardWrite = std::move(write);
}

void WidgetTree::invalidateLayout(EWidgetLayoutInvalidation scope)
{
    // Keep node-local skip proofs coherent with the tree-level dirty bit. The
    // root must descend at least once; individual clean subtrees can still be
    // pruned by UIElement::layoutAssigned().
    _root->_layoutDirtyMask |= 3u;
    const uint8_t bit = static_cast<uint8_t>(scope);
    if ((_layoutInvalidationMask & bit) != 0) {
        _bLayoutDirty = true;
        return;
    }
    switch (scope) {
    case EWidgetLayoutInvalidation::Arrange: ++_arrangeInvalidations; break;
    case EWidgetLayoutInvalidation::Measure: ++_measureInvalidations; break;
    case EWidgetLayoutInvalidation::Structure: ++_structureInvalidations; break;
    }
    _layoutInvalidationMask |= bit;
    _bLayoutDirty = true;
}

bool WidgetTree::emitAction(UIElement& source, std::string_view action)
{
    if (action.empty() || source.getTree() != this) {
        return false;
    }
    // Hold the route and each behaviour list: a handler may detach or
    // release any widget on the way.
    std::vector<UIElementRef> route;
    for (UIElement* node = &source; node; node = node->getParent()) {
        route.push_back(node->shared_from_this());
    }
    for (const UIElementRef& node : route) {
        const std::vector<UIBehaviorRef> behaviors = node->getBehaviors();
        for (const UIBehaviorRef& behavior : behaviors) {
            if (behavior && behavior->getOwner() == node.get() && behavior->onAction(*node, source, action)) {
                return true;
            }
        }
    }
    return _actionSink && _actionSink(source, action);
}

void WidgetTree::tick(float deltaSeconds)
{
    repairPointerSession("tick");
    for (const auto& layer : _layers) {
        tickSubtree(layer.get(), deltaSeconds);
    }
}

void WidgetTree::layout()
{
    const float width  = std::max(static_cast<float>(_logicalExtent.width), kCanvasMinSize);
    const float height = std::max(static_cast<float>(_logicalExtent.height), kCanvasMinSize);
    _root->layoutAssigned(Rect2D{.pos = {0.0f, 0.0f}, .extent = {width, height}});
    _root->clearLayoutDirtyRecursive();
    _bLayoutDirty = false;
    _layoutInvalidationMask = 0;
}

void WidgetTree::markSubtreeResourceReady(UIElement& element)
{
    element.markLayoutDirty(EUIInvalidationReason::ResourceReady);
    for (const auto& child : element.getChildren()) {
        if (child) {
            markSubtreeResourceReady(*child);
        }
    }
}

void WidgetTree::applyFontResourceRevision()
{
    const uint64_t revision = FontManager::get()->resourceRevision();
    if (!_bHasFontRevision) {
        _lastFontRevision = revision;
        _bHasFontRevision = true;
        return;
    }
    if (revision == _lastFontRevision) {
        return;
    }
    _lastFontRevision = revision;
    if (_root) {
        markSubtreeResourceReady(*_root);
    }
}

UIFrameSnapshot WidgetTree::buildSnapshot(const UIFrameBuildContext& ctx)
{
    using clock_t = std::chrono::steady_clock;

    repairPointerSession("buildSnapshot");
    _perfStats = GuiPerfStats{};

    // Font atlas / glyph identity lives on FontManager so the render layer
    // does not take a GUI Reactive dependency. Poll at snapshot start: a
    // revision bump means text metrics or atlas pages changed and every
    // widget must remasure (nested fill containers skip otherwise).
    applyFontResourceRevision();

    // Final target-pixel scale = user zoom (ctx.uiScale) * DPI mapping
    // (_dpiScale). They are orthogonal: uiScale is the app/user zoom, dpiScale
    // maps logical canvas points to framebuffer pixels. The cached draw-item
    // segments hold final target-pixel coordinates, so any change to either
    // factor (or offset/generation) must drop both cache buffers.
    // generation is the host token for resolver *identity* (tests swapping a
    // fake source). Everyday texture ready is path-keyed via the catalog.
    const glm::vec2 effectiveScale = ctx.uiScale * _dpiScale;
    const bool bGenerationChanged =
        _bHasBuildContext && ctx.generation != _lastGeneration;
    const bool bMappingChanged =
        _bHasBuildContext &&
        (effectiveScale != _lastUiScale || ctx.offset != _lastOffset);
    if (bGenerationChanged || bMappingChanged) {
        _itemCache[0].clear();
        _itemCache[1].clear();
        ++_cacheInvalidations;
        _lastInvalidationReason = bMappingChanged
                                      ? EUIInvalidationReason::BuildContextChanged
                                      : EUIInvalidationReason::ResourceReady;
    }
    _bHasBuildContext = true;
    _lastGeneration   = ctx.generation;
    _lastUiScale      = effectiveScale;
    _lastOffset       = ctx.offset;

    _textureCatalog.setSource(_textureSource);
    if (bGenerationChanged) {
        _textureCatalog.dropCachedLookups();
    }
    if (_textureSource) {
        const uint64_t epoch = _textureSource->epoch();
        if (_bHasTextureEpoch && epoch != _lastTextureEpoch) {
            _textureCatalog.refreshFromSource();
        }
        _bHasTextureEpoch = true;
        _lastTextureEpoch = epoch;
    }

    // Pass the DPI-folded scale to the builder: uiScale is the single
    // logical->target-pixel factor it reads. User zoom (ctx.uiScale) and DPI
    // (_dpiScale) stay decoupled up to this point.
    UIFrameBuildContext effectiveCtx = ctx;
    effectiveCtx.uiScale = effectiveScale;
    effectiveCtx.textureCatalog = &_textureCatalog;

    std::chrono::steady_clock::duration layoutDur{};
    if (_bLayoutDirty) {
        const auto layoutStart = clock_t::now();
        layout();
        layoutDur             = clock_t::now() - layoutStart;
        _perfStats.layoutMS   = std::chrono::duration<float, std::milli>(layoutDur).count();
    }

    updateTooltip();

    const auto paintStart = clock_t::now();
    _itemCache[_cacheIndex ^ 1].clear();
    UIFrameBuilder builder(effectiveCtx);
    builder.bindCache(&_itemCache[_cacheIndex], &_itemCache[_cacheIndex ^ 1]);
    _inspectorRecord.resetForFrame();
    _inspectorRecord.targetScale  = effectiveScale;
    _inspectorRecord.targetOffset = effectiveCtx.offset;
    if (YA_GUI_INSPECTOR_IS_ENABLED()) {
        builder.bindInspector(&_inspectorRecord);
    }
    _root->paint(builder);
    _cacheIndex ^= 1;
    const auto paintDur     = clock_t::now() - paintStart;
    _perfStats.paintMS      = std::chrono::duration<float, std::milli>(paintDur).count();
    _perfStats.paintedWidgets = builder.getWidgetCount();
    _perfStats.rebuiltWidgets = builder.getRebuildCount();

    UIFrameSnapshot snapshot = builder.build(_logicalExtent);
    _perfStats.drawItems      = static_cast<uint32_t>(snapshot.items.size());
    _perfStats.layoutSkippedWidgets = _layoutSkippedWidgets;
    ++_frameCounter;

#ifndef NDEBUG
    // Guardrail G2 validation frame: every 60 frames, force a full repaint
    // through an UNBOUND builder (no cache: every widget re-runs paintSelf)
    // and diff it against the incremental result. Any difference means some
    // widget changed paint-relevant state without marking itself dirty —
    // caught here in development instead of shipping a stale frame.
    if ((_frameCounter % 60) == 0) {
        UIFrameBuilder fullBuilder(effectiveCtx); // unbound: hasCachedItems() == false
        _root->paint(fullBuilder);
        const UIFrameSnapshot fullSnapshot = fullBuilder.build(_logicalExtent);
        const auto& incItems  = snapshot.items;
        const auto& fullItems = fullSnapshot.items;
        bool bMismatch = incItems.size() != fullItems.size();
        if (!bMismatch) {
            for (size_t i = 0; i < incItems.size(); ++i) {
                const UIFrameDrawItem& a = incItems[i];
                const UIFrameDrawItem& b = fullItems[i];
                if (!(a == b)) {
                    YA_CORE_ERROR(
                        "GUI validation frame {}: draw item {} differs between incremental "
                        "(kind {} pos ({}, {}) size ({}, {})) and full repaint (kind {} pos ({}, {}) size ({}, {})) — "
                        "a widget changed paint state without marking itself paint-dirty",
                        _frameCounter, i,
                        static_cast<int>(a.kind), a.pos.x, a.pos.y, a.size.x, a.size.y,
                        static_cast<int>(b.kind), b.pos.x, b.pos.y, b.size.x, b.size.y);
                    bMismatch = true;
                    ++_validationMismatches;
                    break;
                }
            }
        }
        if (bMismatch && incItems.size() != fullItems.size()) {
            YA_CORE_ERROR(
                "GUI validation frame {}: incremental paint produced {} items but full repaint produced {} — "
                "a widget changed paint state without marking itself paint-dirty",
                _frameCounter, incItems.size(), fullItems.size());
            ++_validationMismatches;
        }
    }
#endif

    // Invalidation diagnostics: snapshot the cumulative dirty-transition
    // counters so callers can compare frames (and later phases can measure
    // notify/dirty-traversal deltas against the Phase 0 baseline).
    _perfStats.paintDirtyTransitions  = _paintDirtyTransitions;
    _perfStats.layoutDirtyTransitions = _layoutDirtyTransitions;
    _perfStats.cacheInvalidations     = _cacheInvalidations;
    _perfStats.arrangeInvalidations   = _arrangeInvalidations;
    _perfStats.measureInvalidations   = _measureInvalidations;
    _perfStats.structureInvalidations = _structureInvalidations;

    _inspectorRecord.finishDirtyDeltas(_paintDirtyTransitions,
                                       _layoutDirtyTransitions,
                                       _arrangeInvalidations,
                                       _inspectorPrevPaintDirty,
                                       _inspectorPrevLayoutDirty,
                                       _inspectorPrevArrangeDirty);

    // Bridge into the engine-wide perf metrics (aggregated per frame; the
    // per-tree GuiPerfStats stays the per-instance structural view).
    using namespace ya::literals;
    auto& perf = profiling::metrics();
    if (layoutDur.count() > 0) {
        perf.setDuration("gui.tree.layout"_name, "ms"_name, layoutDur);
    }
    perf.setDuration("gui.tree.paint"_name, "ms"_name, paintDur);
    perf.setValue("gui.tree.painted"_name, "count"_name, static_cast<float>(_perfStats.paintedWidgets));
    perf.setValue("gui.tree.rebuilt"_name, "count"_name, static_cast<float>(_perfStats.rebuiltWidgets));
    perf.setValue("gui.tree.items"_name, "count"_name, static_cast<float>(_perfStats.drawItems));
    perf.setValue("gui.tree.pointer_recoveries"_name,
                  "count"_name,
                  static_cast<float>(_pointerSessionRecoveries));

    return snapshot;
}

EWidgetRouteResult WidgetTree::dispatchEvent(const Event& event, const WidgetEventContext& ctx)
{
    pruneTransientState();
    const EEvent::T eventType = event.getEventType();
    preparePointerState(eventType, ctx);
    const bool bPointerEvent = eventType == EEvent::MouseButtonPressed ||
                               eventType == EEvent::MouseButtonReleased ||
                               eventType == EEvent::MouseMoved ||
                               eventType == EEvent::MouseScrolled;

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

    beginPointerDispatch(event);
    const auto finishPointer = [this, &event](EWidgetRouteResult result) {
        endPointerDispatch(event);
        return result;
    };

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
        return finishPointer(EWidgetRouteResult::HandledExclusive);
    }

    if (const EWidgetRouteResult captureResult = dispatchCapturedPointerEvent(event, ctx, eventType);
        captureResult != EWidgetRouteResult::NotHandled) {
        return finishPointer(captureResult);
    }

    // A hover-transparent shield (non-modal popup) is invisible to the user:
    // pointer moves route through it to the visible widget beneath, while
    // presses land on the shield (which dismisses the popup). Both share one
    // single-topmost walk; only the `bForHover` flag differs between them.
    const bool bHoverAware = eventType == EEvent::MouseMoved;
    UIElement* target = hitTestAt(_root.get(), ctx.logicalPoint, bHoverAware);
    refreshPointerPath(target);
    const EWidgetRouteResult result =
        dispatchRoute(target, event, ctx, classifyPointerRoute(buildPath(target)),
                      /*bAppendTrace=*/false);

    if (eventType == EEvent::MouseButtonPressed) {
        _dragCandidate = target;
        _dragCandidateStart = ctx.logicalPoint;
    }
    else if (eventType == EEvent::MouseMoved && _dragCandidate && !_dragCandidate->isAttached()) {
        _dragCandidate = nullptr;
    }
    if (eventType == EEvent::MouseMoved && _dragCandidate && !isDragging()) {
        if (glm::length(ctx.logicalPoint - _dragCandidateStart) > 6.0f) {
            UIElementRef candidate = _dragCandidate->shared_from_this();
            auto operation = candidate->onDragDetected({_dragCandidateStart, ctx.logicalPoint});
            _dragCandidate = nullptr;
            if (operation) {
                beginDrag(candidate.get(), std::move(operation));
                setRouteTrace(EWidgetRoutePolicy::DragSession, candidate.get());
                return finishPointer(EWidgetRouteResult::HandledExclusive);
            }
        }
    }
    if (eventType == EEvent::MouseButtonReleased) {
        _dragCandidate = nullptr;
    }

    // Pressing a non-focusable widget (or empty space) releases focus, so an
    // in-place edit never keeps swallowing keys after the user clicked away.
    if (eventType == EEvent::MouseButtonPressed &&
        (!target || target->_focusPolicy == EWidgetFocusPolicy::None) &&
        _focused != nullptr &&
        !isDescendantOf(target, _focused)) {
        setFocus(nullptr);
    }

    // Hover enter/leave may mutate the tree (menu-bar hover-switch closes and
    // reopens overlays, and opening a new overlay destroys the retired one).
    // Resolve/update hover only after routing so the hit target collected
    // above stays valid for the route above.
    if (eventType == EEvent::MouseMoved || eventType == EEvent::MouseButtonPressed) {
        updateHovered(hoverOwnerAlongPath(
            hitTestAt(_root.get(), ctx.logicalPoint, /*bForHover=*/true),
            ctx.logicalPoint));
    }
    return finishPointer(result);
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
    if (widget) {
        if (_pointerButtonsDown == 0) {
            // Capture is the tree's routing promise for a live press; without
            // one there is no release that could ever end the session, so the
            // request is refused instead of arming a capture that steals the
            // next click.
            YA_CORE_WARN("WidgetTree: refused pointer capture for '{}'; no mouse button is down so "
                         "no release could end the session",
                         widget->_name);
            return;
        }
    }
    _captured = widget;
}

void WidgetTree::releasePointerCapture(UIElement* widget)
{
    if (_captured == widget) {
        _captured = nullptr;
    }
}

bool WidgetTree::hasModalPopup() const
{
    const UIElement* popup = getLayer(ELayer::Popup);
    if (!popup) {
        return false;
    }
    for (const UIElementRef& child : popup->getChildren()) {
        const auto* overlay = dynamic_cast<const UIPopupOverlay*>(child.get());
        if (overlay && overlay->isAttached() && overlay->isModal()) {
            return true;
        }
    }
    return false;
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
        const auto clearPathIfContains = [&](std::vector<std::weak_ptr<UIElement>>& path) {
            for (const auto& weak : path) {
                if (auto p = weak.lock(); p.get() == node) {
                    path.clear();
                    return;
                }
            }
        };
        clearPathIfContains(_pointerPath);
        clearPathIfContains(_focusPath);
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
    return hitTestAt(_root.get(), logicalPoint, /*bForHover=*/false,
                     _bDragSkipSource ? _dragSource : nullptr);
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
        // Disabled subtrees are input-inert: every node on the route must
        // be enabled (P6 subtree-disable contract).
        if (!node->isEnabledInTree()) {
            if (!bAppendTrace) {
                beginRouteTrace(policy, nullptr);
            }
            return EWidgetRouteResult::NotHandled;
        }
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
    _pointerPath.clear();
    for (UIElement* node = target; node != nullptr; node = node->getParent()) {
        _pointerPath.emplace_back(node->shared_from_this());
    }
    std::reverse(_pointerPath.begin(), _pointerPath.end());
}

void WidgetTree::refreshFocusPath()
{
    _focusPath.clear();
    for (UIElement* node = _focused; node != nullptr; node = node->getParent()) {
        _focusPath.emplace_back(node->shared_from_this());
    }
    std::reverse(_focusPath.begin(), _focusPath.end());
}

std::vector<UIElement*> WidgetTree::getPointerPath() const
{
    std::vector<UIElement*> path;
    path.reserve(_pointerPath.size());
    for (const auto& weak : _pointerPath) {
        if (auto node = weak.lock()) {
            path.push_back(node.get());
        }
    }
    return path;
}

std::vector<UIElement*> WidgetTree::getFocusPath() const
{
    std::vector<UIElement*> path;
    path.reserve(_focusPath.size());
    for (const auto& weak : _focusPath) {
        if (auto node = weak.lock()) {
            path.push_back(node.get());
        }
    }
    return path;
}

void WidgetTree::pruneTransientState()
{
    if (_focused && !_focused->isAttached()) {
        _focused->onFocusLost();
        _focused = nullptr;
        _focusPath.clear();
    }
    if (_captured && !_captured->isAttached()) {
        // Liveness sweep: the capture target is gone, so the session cannot
        // continue. Repair it and keep the count visible instead of aborting a
        // frame over input state the platform can no longer complete.
        YA_CORE_WARN("WidgetTree: dropped pointer capture held by detached '{}'; the session "
                     "cannot continue",
                     _captured->_name);
        _captured = nullptr;
        ++_pointerSessionRecoveries;
    }
    if (_hovered && !_hovered->isAttached()) {
        _hovered = nullptr;
    }
    const auto prunePath = [this](std::vector<std::weak_ptr<UIElement>>& path) {
        std::erase_if(path, [this](const std::weak_ptr<UIElement>& weak) {
            const auto node = weak.lock();
            if (!node) {
                return true;
            }
            // The internal tree root is a legal path head but is never
            // "attached" (it has no _tree back-pointer); keep it, drop only
            // detached business widgets.
            return node.get() != _root.get() && !node->isAttached();
        });
    };
    prunePath(_pointerPath);
    prunePath(_focusPath);
}

namespace
{

[[nodiscard]] uint32_t pointerButtonBit(EMouse::T button)
{
    return 1u << static_cast<uint8_t>(button);
}

[[nodiscard]] EMouse::T pointerButtonOf(const Event& event)
{
    const EEvent::T type = event.getEventType();
    if (type == EEvent::MouseButtonPressed) {
        return static_cast<const MouseButtonPressedEvent&>(event).GetMouseButton();
    }
    if (type == EEvent::MouseButtonReleased) {
        return static_cast<const MouseButtonReleasedEvent&>(event).GetMouseButton();
    }
    return EMouse::Left;
}

} // namespace

void WidgetTree::beginPointerDispatch(const Event& event)
{
    if (event.getEventType() != EEvent::MouseButtonPressed) {
        return;
    }
    const EMouse::T button = pointerButtonOf(event);
    const uint32_t  bit    = pointerButtonBit(button);
    if ((_pointerButtonsDown & bit) != 0) {
        // The platform just delivered a press for a button this tree still
        // believes is held, so the release in between never reached the tree
        // (key-focus loss, pointer left the window, the pane was torn into
        // another window, an injected press). The physical state belongs to
        // the platform: recover the stale session and serve this press. A lost
        // release must never eat the click and must never abort the frame.
        cancelPointerSession(std::string{"re-press of "} + EMouse::toString(button) +
                             " with no release in between");
    }
    _pointerButtonsDown |= bit;
}

void WidgetTree::endPointerDispatch(const Event& event)
{
    if (event.getEventType() == EEvent::MouseButtonReleased) {
        _pointerButtonsDown &= ~pointerButtonBit(pointerButtonOf(event));
    }
    repairPointerSession("pointer dispatch");
}

void WidgetTree::cancelPointerSession(std::string_view cause)
{
    const bool bHasSession = _pointerButtonsDown != 0 || _captured != nullptr || isDragging() ||
                             _dragCandidate != nullptr;
    if (!bHasSession) {
        return;
    }

    ++_pointerSessionRecoveries;

    if (isDragging()) {
        // Observers (DockSpace / TreeView / Designer) learn the session ended
        // without a drop, so they roll back instead of keeping a half-drop.
        cancelDrag();
    }
    _dragCandidate = nullptr;

    if (_captured) {
        UIElement* captured = _captured;
        _captured           = nullptr;
        if (captured->isAttached()) {
            // The widget never saw a release: hand it the one terminal
            // notification the framework promises for a cancelled session.
            captured->clearTransientInputState();
        }
    }

    _pointerButtonsDown = 0;
    clearPointerOverState();

    YA_CORE_WARN("WidgetTree: cancelled the live pointer session ({}); a release the platform "
                 "never delivered would otherwise poison the next click",
                 cause);
}

void WidgetTree::reconcilePointerButtons(uint32_t osButtonsDown, std::string_view cause)
{
    if (osButtonsDown == _pointerButtonsDown) {
        return;
    }
    if (osButtonsDown != 0) {
        // A button is still physically held: the session may legitimately
        // continue (cross-window capture / drag). Nothing to repair.
        return;
    }
    // The platform holds no button, so every press this tree cached is over —
    // whether or not its release was ever delivered.
    cancelPointerSession(cause);
}

void WidgetTree::repairPointerSession(std::string_view where)
{
    if (!_captured) {
        return;
    }
    if (!_captured->isAttached()) {
        YA_CORE_WARN("WidgetTree: dropped pointer capture held by detached '{}' ({}); the "
                     "session cannot continue",
                     _captured->_name,
                     where);
        _captured = nullptr;
        ++_pointerSessionRecoveries;
        return;
    }
    if (_pointerButtonsDown == 0) {
        // Capture cannot outlive its press: every button is up, so no widget
        // is still driving a gesture that needs the capture. A widget that
        // keeps capture past the release is dropped here so it cannot eat the
        // next click.
        YA_CORE_WARN("WidgetTree: '{}' still held pointer capture with every button up ({}); "
                     "capture released by the framework",
                     _captured->_name,
                     where);
        _captured = nullptr;
        ++_pointerSessionRecoveries;
    }
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

void WidgetTree::beginDrag(UIElement* source,
                           UIDragDropOperationRef operation,
                           DragSessionObserver observer,
                           bool bShowGhost,
                           bool bSkipSourceInHitTest)
{
    if (isDragging()) {
        cancelDrag();
    }
    if (!operation) {
        return;
    }
    _dragSource  = source;
    // Keep the source alive through the whole session: onDrop may destroy the
    // source subtree (dock floating-window re-sync) before onFinished runs.
    _dragSourceKeepAlive = source ? source->shared_from_this() : nullptr;
    _dragOperation = std::move(operation);
    _dragPoint   = {};
    _dragObserver = std::move(observer);
    _bDragSkipSource = bSkipSourceInHitTest;

    if (!bShowGhost) {
        _dragGhost = nullptr;
        invalidateLayout();
        return;
    }

    _dragGhost = attachDragGhost(_dragOperation->ghostLabel);
    invalidateLayout();
}

UIElement* WidgetTree::findDropTarget(const glm::vec2& logicalPoint) const
{
    return findDropTarget(logicalPoint, _dragOperation.get());
}

UIElement* WidgetTree::findDropTarget(const glm::vec2& logicalPoint,
                                      const UIDragDropOperation* operation) const
{
    if (!operation) {
        return nullptr;
    }
    for (UIElement* node = topmostHit(logicalPoint); node != nullptr; node = node->getParent()) {
        if (node->canAcceptDrop(*operation, logicalPoint)) {
            return node;
        }
    }
    return nullptr;
}

UIElement* WidgetTree::findDropHoverTarget(const glm::vec2& logicalPoint,
                                           const UIDragDropOperation* operation) const
{
    if (!operation) {
        return nullptr;
    }
    if (UIElement* accept = findDropTarget(logicalPoint, operation)) {
        return accept;
    }
    for (UIElement* node = topmostHit(logicalPoint); node != nullptr; node = node->getParent()) {
        if (node->canPreviewDrop(*operation, logicalPoint)) {
            return node;
        }
    }
    return nullptr;
}

UIElementRef WidgetTree::attachDragGhost(const std::string& label)
{
    auto ghost = std::make_shared<UIBorder>("DragGhost");
    ghost->setStyleKey("drag.ghost");
    ghost->setVisibility(EWidgetVisibility::SelfHitTestInvisible);
    FCanvasSlotArgs ghostArgs;
    ghostArgs.offset    = {0.0f, 0.0f};
    ghostArgs.fixedSize = {160.0f, 24.0f};

    auto text = std::make_shared<UIText>("DragGhostLabel");
    text->setText(label);
    text->_fontSize = 13;
    text->_hAlign    = EWidgetAlignH::Center;
    text->_vAlign    = EWidgetAlignV::Center;
    ghost->addDetachedChild(text);
    attachToLayer(ELayer::DragIme, ghost, ghostArgs);
    return ghost;
}

void WidgetTree::placeDragGhost(UIElement& ghost, const glm::vec2& logicalPoint)
{
    if (UIElement* layerHost = getLayer(ELayer::DragIme)) {
        if (UISlot* edge = layerHost->getSlotForChild(ghost); edge && edge->as<UICanvasSlot>()) {
            edge->as<UICanvasSlot>()->setOffset(logicalPoint + glm::vec2(10.0f, 10.0f));
            return;
        }
    }
    YA_CORE_ERROR("WidgetTree drag ghost is missing its canvas slot");
}

void WidgetTree::clearExternalGhost()
{
    if (_externalGhost && _externalGhost->isAttached()) {
        detach(*_externalGhost);
    }
    _externalGhost.reset();
}

void WidgetTree::applyDropTarget(UIElement* target,
                                 const UIDragDropOperation& operation,
                                 const glm::vec2& logicalPoint)
{
    if (target != _dragDropTarget) {
        if (_dragDropTarget) {
            _dragDropTarget->setDropHighlight(false);
        }
        _dragDropTarget = target;
        if (_dragDropTarget) {
            _dragDropTarget->setDropHighlight(true);
            _dragDropTarget->updateDropHover(operation, logicalPoint);
        }
    }
    else if (_dragDropTarget) {
        _dragDropTarget->updateDropHover(operation, logicalPoint);
    }
}

void WidgetTree::updateDrag(const glm::vec2& logicalPoint)
{
    if (!isDragging()) {
        return;
    }
    _dragPoint = logicalPoint;
    if (_dragGhost) {
        placeDragGhost(*_dragGhost, logicalPoint);
    }

    // Hover must see preview-only targets (dock chooser). Commit still uses
    // findDropTarget in endDrag, so a chooser hover cannot drop.
    UIElement* target = findDropHoverTarget(logicalPoint, _dragOperation.get());
    const std::string previousTargetName = _dragDropTarget ? _dragDropTarget->_name : std::string{};
    const bool bTargetChanged = target != _dragDropTarget;
    applyDropTarget(target, *_dragOperation, logicalPoint);

    const std::string currentTargetName = target ? target->_name : std::string{};
    if (bTargetChanged && _dragObserver.onTargetChanged) {
        _dragObserver.onTargetChanged(previousTargetName, currentTargetName);
    }
    if (_dragObserver.onMove) {
        _dragObserver.onMove(*_dragOperation, logicalPoint, currentTargetName);
    }
}

void WidgetTree::clearDragSession()
{
    if (_dragDropTarget) {
        _dragDropTarget->setDropHighlight(false);
        _dragDropTarget = nullptr;
    }
    _externalDropOp = nullptr;
    _dragOperation.reset();
    _dragSource = nullptr;
    _dragCandidate = nullptr;
    _bDragSkipSource = false;
    if (_dragGhost && _dragGhost->isAttached()) {
        detach(*_dragGhost); // operation already cleared: no recursive cancel
    }
    _dragGhost.reset();
    clearExternalGhost();
}

void WidgetTree::endDrag(const glm::vec2& logicalPoint)
{
    if (!isDragging()) {
        return;
    }
    UIElement*       target  = findDropTarget(logicalPoint);
    UIElementRef      targetKeepAlive = target ? target->shared_from_this() : nullptr;
    UIDragDropOperationRef operation = _dragOperation;
    const std::string targetName = target ? target->_name : std::string{};
    DragSessionObserver observer = std::move(_dragObserver);
    // Hold the source alive through onDrop + onFinished: the drop handler can
    // destroy the source widget (dock re-sync) while the finish observer still
    // references it. Released together with the local below on return.
    UIElementRef sourceKeepAlive = std::move(_dragSourceKeepAlive);
    clearDragSession();
    if (target) {
        if (operation) {
            targetKeepAlive->onDrop(*operation, logicalPoint);
        }
    }
    if (observer.onFinished) {
        observer.onFinished(target ? EDragFinishResult::Dropped : EDragFinishResult::NoTarget,
                            logicalPoint, targetName);
    }
}

void WidgetTree::cancelDrag()
{
    if (!isDragging()) {
        return;
    }
    const glm::vec2 logicalPoint = _dragPoint;
    DragSessionObserver observer = std::move(_dragObserver);
    UIElementRef sourceKeepAlive = std::move(_dragSourceKeepAlive);
    clearDragSession();
    if (observer.onFinished) {
        observer.onFinished(EDragFinishResult::Cancelled, logicalPoint, {});
    }
}

void WidgetTree::finishDrag(EDragFinishResult result)
{
    if (!isDragging()) {
        return;
    }
    const glm::vec2 logicalPoint = _dragPoint;
    DragSessionObserver observer = std::move(_dragObserver);
    UIElementRef sourceKeepAlive = std::move(_dragSourceKeepAlive);
    clearDragSession();
    if (observer.onFinished) {
        observer.onFinished(result, logicalPoint, {});
    }
}

void WidgetTree::setExternalDropHover(const UIDragDropOperation& operation,
                                      const glm::vec2& logicalPoint)
{
    if (isDragging()) {
        return;
    }
    _externalDropOp = &operation;
    _dragPoint      = logicalPoint;
    applyDropTarget(findDropHoverTarget(logicalPoint, &operation), operation, logicalPoint);
    if (!_externalGhost) {
        _externalGhost = attachDragGhost(operation.ghostLabel);
    }
    if (_externalGhost) {
        placeDragGhost(*_externalGhost, logicalPoint);
    }
}

void WidgetTree::clearExternalDropHover()
{
    if (_dragDropTarget) {
        _dragDropTarget->setDropHighlight(false);
        _dragDropTarget = nullptr;
    }
    _externalDropOp = nullptr;
    clearExternalGhost();
}

void WidgetTree::setSourceDragChromeVisible(bool visible)
{
    if (_dragGhost) {
        _dragGhost->setVisibility(visible ? EWidgetVisibility::SelfHitTestInvisible
                                          : EWidgetVisibility::Hidden);
    }
    if (!visible && _dragDropTarget) {
        _dragDropTarget->setDropHighlight(false);
        _dragDropTarget = nullptr;
    }
}

bool WidgetTree::dropExternal(const UIDragDropOperation& operation, const glm::vec2& logicalPoint)
{
    if (isDragging()) {
        return false;
    }
    UIElement*  target = findDropTarget(logicalPoint, &operation);
    UIElementRef keep  = target ? target->shared_from_this() : nullptr;
    clearExternalDropHover();
    if (!target) {
        return false;
    }
    keep->onDrop(operation, logicalPoint);
    return true;
}

} // namespace ya
