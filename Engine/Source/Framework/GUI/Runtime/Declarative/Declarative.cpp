#include "GUI/Declarative/Declarative.h"
#include "GUI/Declarative/DeclarativeNodeAdapter.h"

#include "GUI/Widgets/WidgetTree.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace ya::ui
{

UIContainerBuilder column(std::string key, std::string displayName)
{
    return UIContainerBuilder{EWidgetKind::Column, std::move(key), std::move(displayName)};
}

UIContainerBuilder row(std::string key, std::string displayName)
{
    return UIContainerBuilder{EWidgetKind::Row, std::move(key), std::move(displayName)};
}

UIPanelBuilder panel(std::string key, std::string displayName)
{
    return UIPanelBuilder{std::move(key), std::move(displayName)};
}

UITextBuilder text(std::string key, std::string displayName)
{
    return UITextBuilder{std::move(key), std::move(displayName)};
}

UIButtonBuilder button(std::string key, std::string displayName)
{
    return UIButtonBuilder{std::move(key), std::move(displayName)};
}

UITextFieldBuilder textField(std::string key, std::string displayName)
{
    return UITextFieldBuilder{std::move(key), std::move(displayName)};
}

UIReconciler::UIReconciler(WidgetTree& tree, WidgetTree::ELayer layer)
    : _tree(tree)
    , _layer(layer)
{
}

UIElementRef UIReconciler::reconcile(const UIDescription& description)
{
    std::string validationError;
    YA_CORE_ASSERT(validateDescription(description, &validationError), "UIReconciler::reconcile: {}", validationError);
    UIElement* host = _tree.getLayer(_layer);
    YA_CORE_ASSERT(host, "UIReconciler::reconcile: invalid host layer");
    return reconcileNode(*host, description);
}

bool UIReconciler::validateDescription(const UIDescription& description, std::string* error) const
{
    return validateNode(description, "root", error);
}

bool UIReconciler::validateNode(const UIDescription& node, const std::string& path, std::string* error)
{
    std::unordered_set<std::string> keys;
    for (size_t index = 0; index < node.children.size(); ++index) {
        const UIDescription& child = node.children[index];
        const std::string key = identityKey(child);
        if (!key.empty() && !keys.insert(key).second) {
            if (error) {
                *error = std::format("duplicate key '{}' at {}", key, path);
            }
            return false;
        }
        if (!validateNode(child, path + "/" + (key.empty() ? std::to_string(index) : key), error)) {
            return false;
        }
    }
    return true;
}

UIElementRef UIReconciler::findCompatibleChild(UIElement& parent, const UIDescription& node, std::unordered_set<UIElement*>& used) const
{
    const std::string key = identityKey(node);
    for (const auto& child : parent.getChildren()) {
        if (!child || used.contains(child.get())) {
            continue;
        }
        const std::string childKey = child->_stableKey.empty() ? child->_name : child->_stableKey;
        if (childKey == key && UIDeclarativeNodeAdapter::sameKind(*child, node.kind)) {
            used.insert(child.get());
            return child;
        }
    }
    return nullptr;
}

void UIReconciler::applyNodeProperties(UIElement& widget, const UIDescription& node) const
{
    UIDeclarativeNodeAdapter::apply(widget, node);
}

void UIReconciler::reconcileChildren(UIElement& widget, const UIDescription& node)
{
    std::vector<UIDescription> effectiveChildren;
    effectiveChildren.reserve(node.children.size() + 1);

    if (node.kind == EWidgetKind::Button && node._bHasText) {
        UIDescription label{EWidgetKind::Text, node.key + "__label", node.key + "__label"};
        label._text = node._text;
        label._bHasText = true;
        label._position = {0.0f, 0.0f};
        label._bHasPosition = true;
        label._size = {0.0f, 0.0f};
        label._bHasSize = true;
        effectiveChildren.push_back(std::move(label));
    }

    for (const auto& child : node.children) {
        effectiveChildren.push_back(child);
    }

    std::unordered_set<UIElement*> used;
    std::vector<UIElementRef> desired;
    desired.reserve(effectiveChildren.size());

    for (const auto& childDesc : effectiveChildren) {
        UIElementRef child = findCompatibleChild(widget, childDesc, used);
        if (!child) {
            child = UIDeclarativeNodeAdapter::create(childDesc);
            YA_CORE_ASSERT(child, "UIReconciler::reconcileChildren: failed to create child");
            _tree.reparent(widget, child);
            used.insert(child.get());
        }
        applyNodeProperties(*child, childDesc);
        reconcileChildren(*child, childDesc);
        desired.push_back(child);
    }

    for (const auto& existing : widget.getChildren()) {
        if (!existing || used.contains(existing.get())) {
            continue;
        }
        _tree.detach(*existing);
    }

    UIElementRef anchor;
    for (size_t i = desired.size(); i-- > 0;) {
        UIElementRef child = desired[i];
        if (!anchor) {
            if (!isLastChild(widget, *child)) {
                _tree.reparent(widget, child);
            }
        }
        else if (!isChildBefore(widget, *child, *anchor)) {
            _tree.reparentBefore(*anchor, child);
        }
        anchor = child;
    }
}

UIElementRef UIReconciler::reconcileNode(UIElement& parent, const UIDescription& node)
{
    std::unordered_set<UIElement*> used;
    UIElementRef live = findCompatibleChild(parent, node, used);
    if (!live) {
        live = UIDeclarativeNodeAdapter::create(node);
        YA_CORE_ASSERT(live, "UIReconciler::reconcileNode: failed to create node");
        _tree.reparent(parent, live);
    }

    applyNodeProperties(*live, node);
    reconcileChildren(*live, node);
    return live;
}

std::string UIReconciler::identityKey(const UIDescription& node)
{
    return !node.key.empty() ? node.key : node.displayName;
}

bool UIReconciler::isLastChild(const UIElement& parent, const UIElement& child)
{
    const auto& children = parent.getChildren();
    return !children.empty() && children.back().get() == &child;
}

bool UIReconciler::isChildBefore(const UIElement& parent, const UIElement& lhs, const UIElement& rhs)
{
    const auto& children = parent.getChildren();
    const auto lhsIt = std::find_if(children.begin(), children.end(), [&](const UIElementRef& ref) { return ref.get() == &lhs; });
    const auto rhsIt = std::find_if(children.begin(), children.end(), [&](const UIElementRef& ref) { return ref.get() == &rhs; });
    if (lhsIt == children.end() || rhsIt == children.end()) {
        return false;
    }
    return std::distance(children.begin(), lhsIt) < std::distance(children.begin(), rhsIt);
}

UIRenderController::UIRenderController(WidgetTree& tree, WidgetTree::ELayer layer)
    : _tree(tree)
    , _reconciler(tree, layer)
{
}

void UIRenderController::setRenderFunction(UIRenderFunction function)
{
    _renderFunction = std::move(function);
    invalidate();
}

void UIRenderController::invalidate()
{
    _bDirty = true;
}

void UIRenderController::beginBatch()
{
    ++_batchDepth;
}

void UIRenderController::endBatch()
{
    YA_CORE_ASSERT(_batchDepth > 0, "UIRenderController::endBatch without beginBatch");
    --_batchDepth;
}

bool UIRenderController::flush()
{
    if (!_bDirty || _batchDepth != 0 || !_renderFunction) {
        return false;
    }

    UIDescription description = _renderFunction();
    (void)_reconciler.reconcile(description);
    _bDirty = false;
    return true;
}

} // namespace ya::ui
