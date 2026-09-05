#include "GUI/Widgets/UIElement.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GUI/Widgets/Style.h"

#include "Core/Log.h"
#include "Core/Reflection/DeferredInitializer.h"
#include "Core/Reflection/ReflectionSerializer.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Binding/Reactive.h"

#include <algorithm>
#include <atomic>

namespace ya
{
namespace { std::atomic<uint64_t> s_nextWidgetRuntimeId{1}; }

UIElement::UIElement(std::string name, std::string styleKey)
    : _name(std::move(name))
    , _runtimeId(s_nextWidgetRuntimeId.fetch_add(1, std::memory_order_relaxed))
    , _styleKey(std::move(styleKey))
{}

bool UIElement::wantsTick() const
{
    for (const UIBehaviorRef& behavior : _behaviors) {
        if (behavior && behavior->wantsTick()) {
            return true;
        }
    }
    return false;
}

bool UIElement::isAutoSizeActive() const
{
    UISlot* slot = getSlot();
    if (!slot) {
        return false;
    }
    if (const auto* canvas = slot->as<UICanvasSlot>()) {
        return canvas->getWidthSizeMode() == EWidgetSizeMode::Auto ||
               canvas->getHeightSizeMode() == EWidgetSizeMode::Auto;
    }
    if (const auto* box = slot->as<UIBoxSlot>()) {
        return box->getSizeRule() == EUIBoxSlotSizeRule::Auto;
    }
    if (const auto* overlay = slot->as<UIOverlaySlot>()) {
        return overlay->getHAlign() != EUIOverlayAlignment::Fill ||
               overlay->getVAlign() != EUIOverlayAlignment::Fill;
    }
    return false;
}

UIElement::~UIElement()
{
    // A widget must never be destroyed while it still belongs to a live tree
    // (the tree would later walk freed memory). WidgetTree detaches members
    // on tree destruction; direct destruction while attached is a bug.
    YA_CORE_ASSERT(_tree == nullptr, "UIElement destroyed while still attached to a WidgetTree");

    // The visual parent/tree holds children strongly, so a parent normally
    // outlives its children. If this widget dies first (detached subtree
    // where the business drops the root ref), sever the children's back-links
    // so they never dangle into the destroyed parent.
    for (const auto& child : _children) {
        child->_tree   = nullptr;
        child->_parent = nullptr;
    }

    // Sever this widget from every reactive ref it read or bound, so a later
    // set() on a ref never walks a dangling dependent pointer.
    clearDependencies();
    clearPersistentDependencies();
}

void UIElement::addBehavior(const UIBehaviorRef& behavior)
{
    if (!behavior) {
        YA_CORE_ERROR("UIElement::addBehavior: null behavior on '{}'", _name);
        return;
    }
    if (std::find(_behaviors.begin(), _behaviors.end(), behavior) != _behaviors.end()) {
        return;
    }
    if (behavior->_owner && behavior->_owner != this) {
        YA_CORE_ERROR("UIElement::addBehavior: behavior already attached to another widget");
        return;
    }
    _behaviors.push_back(behavior);
    if (isAttached()) {
        behavior->onAttached(*this);
    }
}

void UIElement::removeBehavior(const UIBehavior& behavior)
{
    const auto it = std::find_if(_behaviors.begin(), _behaviors.end(),
                                 [&behavior](const UIBehaviorRef& candidate) { return candidate.get() == &behavior; });
    if (it == _behaviors.end()) {
        return;
    }
    if (isAttached()) {
        (*it)->onDetached(*this);
    }
    _behaviors.erase(it);
}

bool UIElement::hasBehavior(const UIBehavior& behavior) const
{
    return std::any_of(_behaviors.begin(), _behaviors.end(),
                       [&behavior](const UIBehaviorRef& candidate) { return candidate.get() == &behavior; });
}

std::vector<UIElement*> UIElement::getChildrenInPaintOrder() const
{
    std::vector<UIElement*> children;
    children.reserve(_children.size());
    for (const auto& child : _children) {
        children.push_back(child.get());
    }
    std::stable_sort(children.begin(), children.end(), [](const UIElement* a, const UIElement* b) {
        return a->_zOrder < b->_zOrder;
    });
    return children;
}

UISlot* UIElement::getSlot() const
{
    return _slot;
}

UISlot* UIElement::getSlotForChild(const UIElement& child) const
{
    const auto it = std::find_if(_childSlots.begin(), _childSlots.end(),
                                 [&child](const std::unique_ptr<UISlot>& slot) {
                                     return &slot->getChild() == &child;
                                 });
    return it != _childSlots.end() ? it->get() : nullptr;
}

// === Effective-state queries ===

bool UIElement::isVisibleInTree() const
{
    for (const UIElement* node = this; node != nullptr; node = node->_parent) {
        if (!node->isVisibleForRender()) {
            return false;
        }
    }
    return true;
}

bool UIElement::isHitTestableInTree() const
{
    if (!isHitTestableSubtree()) {
        return false;
    }
    for (const UIElement* node = _parent; node != nullptr; node = node->_parent) {
        // Hidden / Collapsed cull rendering and hits; SelfHitTestInvisible
        // culls hits only. HitTestInvisible ancestors do not block children.
        if (!node->isVisibleForRender() || !node->isHitTestableSubtree()) {
            return false;
        }
    }
    return true;
}

bool UIElement::hitTestLayoutRect(const glm::vec2& logicalPoint) const
{
    return logicalPoint.x >= _layoutRect.pos.x &&
           logicalPoint.x <= _layoutRect.pos.x + _layoutRect.extent.x &&
           logicalPoint.y >= _layoutRect.pos.y &&
           logicalPoint.y <= _layoutRect.pos.y + _layoutRect.extent.y;
}

// === Layout ===

Rect2D UIElement::resolveCanvasRect(const Rect2D&    parentRect,
                                    const glm::vec2& anchorMinIn,
                                    const glm::vec2& anchorMaxIn,
                                    const glm::vec2& offset,
                                    const glm::vec2& minSize,
                                    const glm::vec2& maxSize,
                                    const glm::vec2& authoredSize,
                                    glm::bvec2       autoAxis) const
{
    const glm::vec2 anchorMin = glm::clamp(anchorMinIn, 0.0f, 1.0f);
    const glm::vec2 anchorMax = glm::clamp(anchorMaxIn, 0.0f, 1.0f);
    const glm::vec2 rectMin   = parentRect.pos + parentRect.extent * anchorMin + offset;

    // Per-axis size resolution (SizeToContent contract): an axis with an
    // anchor span stretches to the parent; an Auto axis resolves from
    // computeDesiredSize(); otherwise the axis keeps authoredSize from the
    // parent-owned slot.
    const glm::vec2 span    = (anchorMax - anchorMin) * parentRect.extent;
    const glm::vec2 desired = (autoAxis.x || autoAxis.y) ? computeDesiredSize() : authoredSize;
    glm::vec2       size    = authoredSize;
    if (span.x != 0.0f) {
        size.x = span.x;
    }
    else if (autoAxis.x) {
        size.x = desired.x;
    }
    if (span.y != 0.0f) {
        size.y = span.y;
    }
    else if (autoAxis.y) {
        size.y = desired.y;
    }
    size = glm::clamp(size, minSize, maxSize);
    return Rect2D{.pos = rectMin, .extent = size};
}

void UIElement::installLayout(std::unique_ptr<UILayout> layout)
{
    _ownedLayout = std::move(layout);
    _layout      = _ownedLayout.get();
    if (_layout != nullptr) {
        _layout->setOwner(*this);
    }
}

void UIElement::layout(const Rect2D& parentRect)
{
    // All geometry is assigned by the parent edge or the tree root. A plain
    // element therefore accepts the supplied rect verbatim.
    setLayoutRect(parentRect);
    if (_layout != nullptr) {
        _layout->arrange(*this, _layoutRect);
        return;
    }
    layoutChildren(_layoutRect);
}

void UIElement::layoutAssigned(const Rect2D& rect)
{
    if (tryReuseAssignedLayout(rect)) {
        return;
    }
    setLayoutRect(rect);
    if (_layout != nullptr) {
        _layout->arrange(*this, _layoutRect);
        return;
    }
    layoutChildren(_layoutRect);
}

bool UIElement::tryReuseAssignedLayout(const Rect2D& rect)
{
    Rect2D clamped = rect;
    clamped.extent = glm::max(clamped.extent, glm::vec2(0.0f));
    if (_assignedLayoutRevision == _layoutRevision && _layoutDirtyMask == 0 &&
        clamped.pos == _layoutRect.pos &&
        clamped.extent == _layoutRect.extent) {
        if (_tree) {
            ++_tree->_layoutSkippedWidgets;
        }
        return true;
    }
    return false;
}

void UIElement::layoutChildren(const Rect2D& layoutRect)
{
    for (UIElement* child : getChildrenInPaintOrder()) {
        if (child->participatesInLayout()) {
            child->layoutAssigned(layoutRect);
        }
    }
}

glm::vec2 UIElement::computeDesiredSize() const
{
    // A host measures through its layout; a leaf reports intrinsic content.
    // Authored size is parent-owned slot state, not a child layout input.
    if (_layout != nullptr) {
        return _layout->measure(*this);
    }
    return computeIntrinsicSize();
}

glm::vec2 UIElement::computeIntrinsicSize() const
{
    return {0.0f, 0.0f};
}

// === Paint ===

void UIElement::paint(UIFrameBuilder& builder)
{
    if (!isVisibleForRender()) {
        return;
    }
    // Guardrail G1: every widget paints inside its own rect by default, so
    // overflow can never draw over siblings/status bars. Opt out via
    // _bSelfClip only when a widget legitimately paints outside its rect.
    const bool bSelfClip = _bSelfClip;
    if (bSelfClip) {
        builder.pushClip(_layoutRect);
    }
    builder.countWidget();
    PaintScope paintScope(this);
    if (_bVolatile || _bPaintDirty || !builder.hasCachedItems(this)) {
        clearDependencies();
        builder.countRebuild();
        const size_t start = builder.getItemCount();
        paintSelf(builder);
        builder.cacheItems(this, start);
        _bPaintDirty = false;
    }
    else {
        builder.reuseCachedItems(this);
    }
    paintChildren(builder);
    if (bSelfClip) {
        builder.popClip();
    }
}

void UIElement::clearDependencies()
{
    for (ReactiveBase* ref : _paintDependencies) {
        ref->removePaintDependent(this);
    }
    _paintDependencies.clear();
}

void UIElement::clearPersistentDependencies()
{
    for (ReactiveBase* ref : _persistentDependencies) {
        ref->removePersistentDependent(this);
    }
    _persistentDependencies.clear();
}

void UIElement::markPaintDirty(EUIInvalidationReason reason)
{
    if (_bPaintDirty) {
        return; // already dirty: no transition to count
    }
    _bPaintDirty = true;
    if (reason != EUIInvalidationReason::None) {
        _lastInvalidationReason = reason;
    }
    if (_tree) {
        ++_tree->_paintDirtyTransitions;
        if (reason != EUIInvalidationReason::None) {
            _tree->_lastInvalidationReason = reason;
        }
    }
}

void UIElement::markLayoutDirty(EUIInvalidationReason reason)
{
    ++_layoutRevision;
    _layoutDirtyMask |= 2u;
    markPaintDirty(reason);
    for (UIElement* ancestor = _parent; ancestor != nullptr; ancestor = ancestor->_parent) {
        ancestor->_layoutDirtyMask |= 2u;
    }
    if (_tree) {
        // Count only the clean->dirty layout edge (the tree may already be
        // layout-dirty from an earlier mark in the same frame).
        if (!_tree->_bLayoutDirty) {
            ++_tree->_layoutDirtyTransitions;
        }
        _tree->invalidateLayout(EWidgetLayoutInvalidation::Measure);
    }
}

void UIElement::markArrangeDirty(EUIInvalidationReason reason)
{
    ++_layoutRevision;
    _layoutDirtyMask |= 1u;
    markPaintDirty(reason);
    for (UIElement* ancestor = _parent; ancestor != nullptr; ancestor = ancestor->_parent) {
        ancestor->_layoutDirtyMask |= 1u;
    }
    if (_tree) {
        _tree->invalidateLayout(EWidgetLayoutInvalidation::Arrange);
    }
}

void UIElement::clearLayoutDirtyRecursive()
{
    _layoutDirtyMask = 0;
    for (const auto& child : _children) {
        child->clearLayoutDirtyRecursive();
    }
}

void UIElement::invalidateSubtree(EUIInvalidationReason reason)
{
    markPaintDirty(reason);
    for (const auto& child : _children) {
        child->invalidateSubtree(reason);
    }
}

void UIElement::setStyleKey(std::string value)
{
    if (_styleKey == value) {
        return;
    }
    diagnoseStyleKey(value, getStyleTypeIndex());
    _styleKey = std::move(value);
    invalidateProperty(EUIPropertyImpact::Layout);
}

void UIElement::invalidateProperty(EUIPropertyImpact impact)
{
    switch (impact) {
    case EUIPropertyImpact::None:
        break;
    case EUIPropertyImpact::Paint:
        markPaintDirty(EUIInvalidationReason::PaintProperty);
        break;
    case EUIPropertyImpact::Layout:
        markLayoutDirty(EUIInvalidationReason::LayoutProperty);
        break;
    case EUIPropertyImpact::SubtreePaintContext:
        invalidateSubtree();
        break;
    }
}

void UIElement::paintChildren(UIFrameBuilder& builder)
{
    for (UIElement* child : getChildrenInPaintOrder()) {
        child->paint(builder);
    }
}

// === Events ===

bool UIElement::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    for (const UIBehaviorRef& behavior : _behaviors) {
        if (behavior && behavior->handleInputEvent(*this, event, ctx)) {
            return true;
        }
    }
    (void)event;
    (void)ctx;
    return false; // Passive: base/panels/text never consume events.
}

bool UIElement::previewInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    for (const UIBehaviorRef& behavior : _behaviors) {
        if (behavior && behavior->previewInputEvent(*this, event, ctx)) {
            return true;
        }
    }
    (void)event;
    (void)ctx;
    return false;
}

bool UIElement::bubbleInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    for (const UIBehaviorRef& behavior : _behaviors) {
        if (behavior && behavior->bubbleInputEvent(*this, event, ctx)) {
            return true;
        }
    }
    return handleInputEvent(event, ctx);
}

void UIElement::tick(float deltaSeconds)
{
    for (const UIBehaviorRef& behavior : _behaviors) {
        if (behavior && behavior->wantsTick()) {
            behavior->tick(*this, deltaSeconds);
        }
    }
}

// === Authoring ===

void UIElement::addDetachedChild(const UIElementRef& child)
{
    addDetachedChild(child, [](UIElement&, UISlot&) {});
}

void UIElement::addDetachedChild(const UIElementRef& child, FChildSlotInitializer init)
{
    if (!child) {
        YA_CORE_ERROR("UIElement::addDetachedChild: null child");
        return;
    }
    if (child.get() == this) {
        YA_CORE_ERROR("UIElement::addDetachedChild: cannot add a widget to itself");
        return;
    }
    if (child->isAttached() || child->_parent != nullptr) {
        YA_CORE_ERROR("UIElement::addDetachedChild: child '{}' already has a parent; "
                      "authoring requires a detached child",
                      child->_name);
        return;
    }
    appendChildEdge(child, std::move(init));
}

void UIElement::initializeChildSlot(UIElement& child, FChildSlotInitializer init)
{
    if (UISlot* slot = getSlotForChild(child)) {
        init(child, *slot);
    }
}

std::unique_ptr<UISlot> UIElement::createSlotForChild(UIElement& child)
{
    // The installed layout owns the edge type. Hosts that only install a
    // layout must not have to repeat this factory, otherwise they silently
    // get a base UISlot and arrange cannot see typed intent.
    if (_layout != nullptr) {
        return _layout->createSlot(*this, child);
    }
    return std::make_unique<UISlot>(*this, child);
}

void UIElement::appendChildEdge(const UIElementRef& child)
{
    appendChildEdge(child, [](UIElement&, UISlot&) {});
}

void UIElement::appendChildEdge(const UIElementRef& child, FChildSlotInitializer init)
{
    insertChildEdge(_children.size(), child, std::move(init));
}

void UIElement::insertChildEdge(size_t index, const UIElementRef& child)
{
    insertChildEdge(index, child, [](UIElement&, UISlot&) {});
}

void UIElement::insertChildEdge(size_t index, const UIElementRef& child, FChildSlotInitializer init)
{
    child->_parent = this;
    std::unique_ptr<UISlot> slot = createSlotForChild(*child);
    child->_slot = slot.get();
    if (slot) {
        init(*child, *slot);
    }
    const size_t insertAt = std::min(index, _children.size());
    _children.insert(_children.begin() + static_cast<std::ptrdiff_t>(insertAt), child);
    _childSlots.insert(_childSlots.begin() + static_cast<std::ptrdiff_t>(insertAt), std::move(slot));
    finalizeInsertedChild(child);
}

void UIElement::finalizeInsertedChild(const UIElementRef& child)
{
    if (_tree) {
        WidgetTree::markSubtreeMembership(child.get(), _tree);
    }
}

void UIElement::removeChildEdge(UIElement& child)
{
    const auto childIt = std::find_if(_children.begin(), _children.end(),
                                      [&child](const UIElementRef& ref) { return ref.get() == &child; });
    if (childIt == _children.end()) {
        return;
    }
    const size_t index = static_cast<size_t>(std::distance(_children.begin(), childIt));
    _children.erase(childIt);
    if (index < _childSlots.size()) {
        _childSlots.erase(_childSlots.begin() + static_cast<std::ptrdiff_t>(index));
    }
    child._parent = nullptr;
    child._slot   = nullptr;
}

// === Field serialization ===

nlohmann::json UIElement::serializeFields() const
{
    ::ya::reflection::DeferredInitializerQueue::instance().executeAll();
    auto* cls = ClassRegistry::instance().getClass(getTypeIndex());
    if (!cls) {
        return nlohmann::json();
    }
    nlohmann::json j = ReflectionSerializer::serializeByRuntimeReflection(this, getTypeIndex(), cls->getName());
    nlohmann::json authored = serializeAuthoredStyle();
    if (!authored.is_null()) {
        j["_authoredStyle"] = std::move(authored);
    }
    return j;
}

void UIElement::deserializeFields(const nlohmann::json& fields)
{
    ::ya::reflection::DeferredInitializerQueue::instance().executeAll();
    nlohmann::json rest = fields.is_object() ? fields : nlohmann::json::object();
    nlohmann::json authored;
    const bool bHasAuthored = rest.contains("_authoredStyle");
    if (bHasAuthored) {
        authored = rest["_authoredStyle"];
        rest.erase("_authoredStyle");
    }
    auto* cls = ClassRegistry::instance().getClass(getTypeIndex());
    if (cls) {
        ReflectionSerializer::deserializeByRuntimeReflection(this, getTypeIndex(), rest, cls->getName());
    }
    if (bHasAuthored) {
        deserializeAuthoredStyle(authored);
    }

    diagnoseStyleKey(_styleKey, getStyleTypeIndex());

    // Mutation transaction boundary (GI-201): reflection writes bypass the
    // changed-only setters (direct memory access), so no per-field invalidation
    // fires and no binding observer sees an intermediate state. Aggregate the
    // highest impact once, after the object invariants are complete.
    // Detached authoring (UIDocument::instantiate) is a no-op here — the tree
    // is null and attach() invalidates layout on the instantiate->attach path;
    // a live-tree bulk restore (editor) re-layouts + repaints exactly once.
    invalidateProperty(EUIPropertyImpact::Layout);
}

void UIElement::appendRuntimeDiagnostics(nlohmann::json& node, const WidgetTree& tree) const
{
    (void)node;
    (void)tree;
}

bool UIElement::canAcceptDrop(const UIDragDropOperation& operation, const glm::vec2& logicalPoint)
{
    for (const UIBehaviorRef& behavior : _behaviors) {
        if (behavior && behavior->canAcceptDrop(*this, operation, logicalPoint)) {
            return true;
        }
    }
    return false;
}

void UIElement::onDrop(const UIDragDropOperation& operation, const glm::vec2& logicalPoint)
{
    for (const UIBehaviorRef& behavior : _behaviors) {
        if (behavior && behavior->canAcceptDrop(*this, operation, logicalPoint)) {
            behavior->onDrop(*this, operation, logicalPoint);
            return;
        }
    }
}

void UIElement::setDropHighlight(bool bHighlight)
{
    for (const UIBehaviorRef& behavior : _behaviors) {
        if (behavior) {
            behavior->setDropHighlight(*this, bHighlight);
        }
    }
}

void UIElement::updateDropHover(const UIDragDropOperation& operation, const glm::vec2& logicalPoint)
{
    for (const UIBehaviorRef& behavior : _behaviors) {
        if (behavior && behavior->canAcceptDrop(*this, operation, logicalPoint)) {
            behavior->updateDropHover(*this, operation, logicalPoint);
            return;
        }
    }
}

bool UIElement::beginDragOperation(UIDragDropOperationRef operation,
                                   bool bShowGhost,
                                   bool bSkipSourceInHitTest)
{
    if (!_tree || !operation) {
        return false;
    }
    _tree->beginDrag(this, std::move(operation), {}, bShowGhost, bSkipSourceInHitTest);
    return true;
}

UIDragDropOperationRef UIElement::onDragDetected(const FDragDetectedEvent& event)
{
    for (const UIBehaviorRef& behavior : _behaviors) {
        if (behavior) {
            if (UIDragDropOperationRef operation = behavior->onDragDetected(*this, event)) {
                return operation;
            }
        }
    }
    return nullptr;
}

void UIElement::appendRuntimeLayoutDiagnostics(nlohmann::json& node) const
{
    (void)node;
}

} // namespace ya

// Enum reflection for serialization (must register at global scope; the
// EWidget* names are distinct from the legacy EUI* enums while both modules
// coexist).
YA_REFLECT_ENUM_BEGIN(ya::EWidgetAlignH)
YA_REFLECT_ENUM_VALUE(Left)
YA_REFLECT_ENUM_VALUE(Center)
YA_REFLECT_ENUM_VALUE(Right)
YA_REFLECT_ENUM_END()

YA_REFLECT_ENUM_BEGIN(ya::EWidgetAlignV)
YA_REFLECT_ENUM_VALUE(Top)
YA_REFLECT_ENUM_VALUE(Center)
YA_REFLECT_ENUM_VALUE(Bottom)
YA_REFLECT_ENUM_END()

YA_REFLECT_ENUM_BEGIN(ya::EWidgetHitFilter)
YA_REFLECT_ENUM_VALUE(Pass)
YA_REFLECT_ENUM_VALUE(Stop)
YA_REFLECT_ENUM_END()

YA_REFLECT_ENUM_BEGIN(ya::EWidgetVisibility)
YA_REFLECT_ENUM_VALUE(Visible)
YA_REFLECT_ENUM_VALUE(Hidden)
YA_REFLECT_ENUM_VALUE(Collapsed)
YA_REFLECT_ENUM_VALUE(HitTestInvisible)
YA_REFLECT_ENUM_VALUE(SelfHitTestInvisible)
YA_REFLECT_ENUM_END()

YA_REFLECT_ENUM_BEGIN(ya::EWidgetBoxLayout)
YA_REFLECT_ENUM_VALUE(Horizontal)
YA_REFLECT_ENUM_VALUE(Vertical)
YA_REFLECT_ENUM_END()

YA_REFLECT_ENUM_BEGIN(ya::EWidgetMainAxisAlignment)
YA_REFLECT_ENUM_VALUE(Start)
YA_REFLECT_ENUM_VALUE(Center)
YA_REFLECT_ENUM_VALUE(End)
YA_REFLECT_ENUM_END()

YA_REFLECT_ENUM_BEGIN(ya::EWidgetFocusPolicy)
YA_REFLECT_ENUM_VALUE(None)
YA_REFLECT_ENUM_VALUE(Focusable)
YA_REFLECT_ENUM_END()
