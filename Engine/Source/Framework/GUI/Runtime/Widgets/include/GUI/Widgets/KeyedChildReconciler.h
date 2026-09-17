#pragma once

#include "GUI/Layout/UISlot.h"
#include "GUI/Widgets/UIElement.h"
#include "GUI/Widgets/WidgetTree.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ya
{

/// Reconciles a keyed set of already-materialized child widgets under one
/// attached parent. Keys own identity; positions are only the current order.
/// The reconciler never rebuilds a widget whose key survives a reconcile.
class YA_GUI_API UIKeyedChildReconciler final
{
  public:
    using Factory = std::function<UIElementRef(const std::string& key, size_t index)>;
    using Updater = std::function<void(UIElement& child, const std::string& key, size_t index)>;
    using SlotBinder = std::function<void(UISlot& slot, const std::string& key, size_t index)>;

    UIKeyedChildReconciler(WidgetTree& tree, UIElement& parent, Factory factory);

    [[nodiscard]] bool reconcile(const std::vector<std::string>& keys);
    [[nodiscard]] bool reconcile(const std::vector<std::string>& keys, Updater updater);
    [[nodiscard]] bool reconcile(const std::vector<std::string>& keys, Updater updater, SlotBinder slotBinder);
    [[nodiscard]] UIElementRef find(const std::string& key) const;
    [[nodiscard]] size_t size() const { return _children.size(); }

  private:
    WidgetTree* _tree = nullptr;
    UIElement* _parent = nullptr;
    Factory _factory;
    std::unordered_map<std::string, UIElementRef> _children;
};

} // namespace ya
