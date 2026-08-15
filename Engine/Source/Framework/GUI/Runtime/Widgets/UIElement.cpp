#include "GUI/Widgets/UIElement.h"

#include "Core/Log.h"
#include "Core/Reflection/ReflectionSerializer.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Reactive.h"
#include "GUI/Widgets/WidgetTree.h"

#include <algorithm>

namespace ya
{

UIElement::UIElement(std::string name) : _name(std::move(name)) {}

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

    // Sever this widget from every reactive ref it read, so a later set() on
    // a ref never walks a dangling dependent pointer.
    clearDependencies();
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
    return _parent ? _parent->getSlotForChild(*this) : nullptr;
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

Rect2D UIElement::computeAnchorRect(const Rect2D& parentRect) const
{
    const glm::vec2 anchorMin = glm::clamp(_anchorMin, 0.0f, 1.0f);
    const glm::vec2 anchorMax = glm::clamp(_anchorMax, 0.0f, 1.0f);
    const glm::vec2 rectMin   = parentRect.pos + parentRect.extent * anchorMin + _position;

    // Per-axis size resolution (SizeToContent contract): an axis with an
    // anchor span stretches to the parent; an AutoSize axis resolves from
    // computeDesiredSize(); otherwise the axis keeps _size (default {0,0}
    // anchors = legacy absolute layout).
    const glm::vec2 span    = (anchorMax - anchorMin) * parentRect.extent;
    const glm::vec2 desired = _bAutoSize ? computeDesiredSize() : _size;
    glm::vec2       size    = _size;
    if (span.x != 0.0f) {
        size.x = span.x;
    }
    else if (_bAutoSize) {
        size.x = desired.x;
    }
    if (span.y != 0.0f) {
        size.y = span.y;
    }
    else if (_bAutoSize) {
        size.y = desired.y;
    }
    return Rect2D{.pos = rectMin, .extent = size};
}

void UIElement::layout(const Rect2D& parentRect)
{
    setLayoutRect(computeAnchorRect(parentRect));
    layoutChildren(_layoutRect);
}

void UIElement::layoutAssigned(const Rect2D& rect)
{
    setLayoutRect(rect);
    layoutChildren(_layoutRect);
}

void UIElement::layoutChildren(const Rect2D& layoutRect)
{
    for (UIElement* child : getChildrenInPaintOrder()) {
        if (child->participatesInLayout()) {
            child->layout(layoutRect);
        }
    }
}

glm::vec2 UIElement::computeDesiredSize() const
{
    return _size;
}

// === Paint ===

void UIElement::paint(UIFrameBuilder& builder)
{
    if (!isVisibleForRender()) {
        return;
    }
    builder.countWidget();
    pushPaintWidget(this);
    if (_bPaintDirty || !builder.hasCachedItems(this)) {
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
    popPaintWidget();
}

void UIElement::clearDependencies()
{
    for (ReactiveBase* ref : _dependencies) {
        ref->removeDependent(this);
    }
    _dependencies.clear();
}

void UIElement::markLayoutDirty()
{
    markPaintDirty();
    if (_tree) {
        _tree->invalidateLayout();
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
    (void)event;
    (void)ctx;
    return false; // Passive: base/panels/text never consume events.
}

// === Authoring ===

void UIElement::addDetachedChild(const UIElementRef& child)
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
    appendChildEdge(child);
}

std::unique_ptr<UISlot> UIElement::createSlotForChild(UIElement& child)
{
    return std::make_unique<UISlot>(*this, child);
}

void UIElement::appendChildEdge(const UIElementRef& child)
{
    child->_parent = this;
    _children.push_back(child);
    _childSlots.push_back(createSlotForChild(*child));
    if (_tree) {
        WidgetTree::markSubtreeMembership(child.get(), _tree);
    }
}

void UIElement::insertChildEdge(size_t index, const UIElementRef& child)
{
    child->_parent = this;
    const size_t insertAt = std::min(index, _children.size());
    _children.insert(_children.begin() + static_cast<std::ptrdiff_t>(insertAt), child);
    _childSlots.insert(_childSlots.begin() + static_cast<std::ptrdiff_t>(insertAt), createSlotForChild(*child));
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
}

// === Field serialization ===

nlohmann::json UIElement::serializeFields() const
{
    auto* cls = ClassRegistry::instance().getClass(getTypeIndex());
    if (!cls) {
        return nlohmann::json();
    }
    return ReflectionSerializer::serializeByRuntimeReflection(this, getTypeIndex(), cls->getName());
}

void UIElement::deserializeFields(const nlohmann::json& fields)
{
    auto* cls = ClassRegistry::instance().getClass(getTypeIndex());
    if (!cls) {
        return;
    }
    ReflectionSerializer::deserializeByRuntimeReflection(this, getTypeIndex(), fields, cls->getName());
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
