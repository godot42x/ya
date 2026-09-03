#include "GUI/Widgets/KeyedChildReconciler.h"

#include "Core/Log.h"

#include <unordered_set>

namespace ya
{

UIKeyedChildReconciler::UIKeyedChildReconciler(WidgetTree& tree, UIElement& parent, Factory factory)
    : _tree(&tree), _parent(&parent), _factory(std::move(factory))
{
}

bool UIKeyedChildReconciler::reconcile(const std::vector<std::string>& keys)
{
    return reconcile(keys, {}, {});
}

bool UIKeyedChildReconciler::reconcile(const std::vector<std::string>& keys, Updater updater)
{
    return reconcile(keys, std::move(updater), {});
}

bool UIKeyedChildReconciler::reconcile(const std::vector<std::string>& keys, Updater updater, SlotBinder slotBinder)
{
    if (!_tree || !_parent || !_parent->isAttached() || !_factory) {
        YA_CORE_ERROR("UIKeyedChildReconciler: parent must be attached and factory must be valid");
        return false;
    }

    std::unordered_set<std::string> requested;
    requested.reserve(keys.size());
    for (const std::string& key : keys) {
        if (key.empty() || !requested.insert(key).second) {
            YA_CORE_ERROR("UIKeyedChildReconciler: empty or duplicate key '{}' rejected", key);
            return false;
        }
    }

    for (auto it = _children.begin(); it != _children.end();) {
        if (!requested.contains(it->first)) {
            if (it->second && it->second->isAttached()) {
                _tree->detach(*it->second);
            }
            it = _children.erase(it);
        }
        else {
            ++it;
        }
    }

    for (size_t index = 0; index < keys.size(); ++index) {
        const std::string& key = keys[index];
        if (_children.contains(key)) {
            continue;
        }
        UIElementRef child = _factory(key, index);
        if (!child || child->isAttached()) {
            YA_CORE_ERROR("UIKeyedChildReconciler: factory failed for key '{}'", key);
            return false;
        }
        child->_stableKey = key;
        if (!_tree->attach(*_parent, child).valid()) {
            YA_CORE_ERROR("UIKeyedChildReconciler: attach failed for key '{}'", key);
            return false;
        }
        _children.emplace(key, std::move(child));
    }

    for (size_t index = 0; index < keys.size(); ++index) {
        UIElement& child = *_children.at(keys[index]);
        if (slotBinder) {
            if (UISlot* slot = _parent->getSlotForChild(child)) {
                slotBinder(*slot, keys[index], index);
            }
        }
        if (updater) {
            updater(child, keys[index], index);
        }
    }

    for (size_t index = 0; index < keys.size(); ++index) {
        const auto current = _parent->getChildren();
        if (index >= current.size()) {
            return false;
        }
        UIElementRef desired = _children.at(keys[index]);
        if (current[index].get() != desired.get()) {
            _tree->reparentBefore(*current[index], desired);
        }
    }
    return true;
}

UIElementRef UIKeyedChildReconciler::find(const std::string& key) const
{
    const auto it = _children.find(key);
    return it == _children.end() ? nullptr : it->second;
}

} // namespace ya
