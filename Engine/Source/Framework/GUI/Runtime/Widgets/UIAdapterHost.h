#pragma once

#include "GUI/Widgets/WidgetTree.h"

#include <functional>

namespace ya
{

/// Minimal host boundary for future document/component adapters.
///
/// Native DSL is one authoring path (ui::build), while adapters can mount a
/// retained subtree once and later issue changed-only patches against the same
/// live root. The host owns no adapter-specific diff/model logic: it only
/// bridges mount / patch / unmount to the existing WidgetTree kernel.
class YA_GUI_API UIAdapterHost
{
  public:
    UIAdapterHost(WidgetTree& tree, UIElement& parent) : _tree(tree), _parent(parent) {}

    UIAdapterHost(const UIAdapterHost&) = delete;
    UIAdapterHost& operator=(const UIAdapterHost&) = delete;

    [[nodiscard]] WidgetTree& getTree() const { return _tree; }
    [[nodiscard]] UIElement&  getParent() const { return _parent; }
    [[nodiscard]] UIElement*  getRoot() const { return _root.get(); }

    UIElement& mount(UIElementRef root)
    {
        YA_CORE_ASSERT(root, "UIAdapterHost::mount requires a root widget");
        if (_root) {
            unmount();
        }

        UIElement* const rawRoot = root.get();
        const WidgetAttachment attached = _tree.attach(_parent, root);
        YA_CORE_ASSERT(attached.valid(), "UIAdapterHost::mount: attach failed for '{}'", rawRoot->_name);
        _root = std::move(root);
        return *rawRoot;
    }

    void patch(const std::function<void(UIElement&)>& apply)
    {
        YA_CORE_ASSERT(_root, "UIAdapterHost::patch requires a mounted root");
        YA_CORE_ASSERT(apply, "UIAdapterHost::patch requires a valid patch callback");
        apply(*_root);
    }

    void unmount()
    {
        if (!_root) {
            return;
        }
        if (_root->getParent()) {
            _tree.detach(*_root);
        }
        _root.reset();
    }

  private:
    WidgetTree&  _tree;
    UIElement&   _parent;
    UIElementRef _root;
};

} // namespace ya
