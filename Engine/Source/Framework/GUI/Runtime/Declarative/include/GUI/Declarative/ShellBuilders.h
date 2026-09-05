#pragma once

// Builders for the shell / custom controls that have no dedicated
// ControlBuilders entry: MenuBar, TreeView and DockSpace. These are grafted
// into a DSL page via `child(UIElementRef)`-style composition, so their
// builders only expose the construct-time properties a page author needs.

#include "GUI/Declarative/BuilderBase.h"

#include "GUI/Widgets/Controls/DockSpace/DockSpace.h"
#include "GUI/Widgets/Controls/Menu.h"
#include "GUI/Widgets/Controls/MenuBar.h"
#include "GUI/Widgets/Controls/PopupOverlay.h"
#include "GUI/Widgets/Controls/TreeView.h"

namespace ya::ui
{

/// Horizontal menu bar. Items are added imperatively through the live widget
/// (addItem owns the menu factory lifetime), so the builder carries no item
/// API.
class UIMenuBarWidgetBuilder final : public TUIWidgetBuilder<UIMenuBar, UIMenuBarWidgetBuilder>
{
  public:
    explicit UIMenuBarWidgetBuilder(std::string key, std::string displayName = {})
        : TUIWidgetBuilder(kTypeIdMenuBar, std::move(key), std::move(displayName))
    {
    }
};

/// Data-driven tree view. The data source is a ReactiveList<FNode> bound at
/// construct time; selection changes keep going through the live widget.
class UITreeViewWidgetBuilder final : public TUIWidgetBuilder<UITreeView, UITreeViewWidgetBuilder>
{
  public:
    explicit UITreeViewWidgetBuilder(std::string key, std::string displayName = {})
        : TUIWidgetBuilder(kTypeIdTreeView, std::move(key), std::move(displayName))
    {
    }

    [[nodiscard]] UITreeViewWidgetBuilder& bindData(std::shared_ptr<ReactiveList<UITreeView::FNode>> value) &
    {
        _widget->bindData(std::move(value));
        return *this;
    }

    [[nodiscard]] UITreeViewWidgetBuilder&& bindData(std::shared_ptr<ReactiveList<UITreeView::FNode>> value) &&
    {
        _widget->bindData(std::move(value));
        return std::move(*this);
    }

    [[nodiscard]] UITreeViewWidgetBuilder& bindSelection(std::shared_ptr<Reactive<std::string>> value) &
    {
        _widget->bindSelection(std::move(value));
        return *this;
    }

    [[nodiscard]] UITreeViewWidgetBuilder&& bindSelection(std::shared_ptr<Reactive<std::string>> value) &&
    {
        _widget->bindSelection(std::move(value));
        return std::move(*this);
    }

    [[nodiscard]] UITreeViewWidgetBuilder& setOnSelectionChanged(std::function<void(const std::string&)> value) &
    {
        _widget->_onSelectionChanged = std::move(value);
        return *this;
    }

    [[nodiscard]] UITreeViewWidgetBuilder&& setOnSelectionChanged(std::function<void(const std::string&)> value) &&
    {
        _widget->_onSelectionChanged = std::move(value);
        return std::move(*this);
    }

    [[nodiscard]] UITreeViewWidgetBuilder& bindFilter(std::shared_ptr<Reactive<std::string>> value) &
    {
        _widget->bindFilter(std::move(value));
        return *this;
    }

    [[nodiscard]] UITreeViewWidgetBuilder&& bindFilter(std::shared_ptr<Reactive<std::string>> value) &&
    {
        _widget->bindFilter(std::move(value));
        return std::move(*this);
    }

    [[nodiscard]] UITreeViewWidgetBuilder& setReorderable(bool value) &
    {
        _widget->setReorderable(value);
        return *this;
    }

    [[nodiscard]] UITreeViewWidgetBuilder&& setReorderable(bool value) &&
    {
        _widget->setReorderable(value);
        return std::move(*this);
    }

    [[nodiscard]] UITreeViewWidgetBuilder& setOnReorderHandler(
        std::function<void(const std::string&, const std::string&, int)> value) &
    {
        _widget->setOnReorderHandler(std::move(value));
        return *this;
    }

    [[nodiscard]] UITreeViewWidgetBuilder&& setOnReorderHandler(
        std::function<void(const std::string&, const std::string&, int)> value) &&
    {
        _widget->setOnReorderHandler(std::move(value));
        return std::move(*this);
    }
};

/// Dock space projecting an FDockContext dock tree. The context is the
/// session owner and is bound at construct time.
class UIDockSpaceWidgetBuilder final : public TUIWidgetBuilder<UIDockSpace, UIDockSpaceWidgetBuilder>
{
  public:
    explicit UIDockSpaceWidgetBuilder(std::string key, std::string displayName = {})
        : TUIWidgetBuilder(kTypeIdDockSpace, std::move(key), std::move(displayName))
    {
    }

    [[nodiscard]] UIDockSpaceWidgetBuilder& setContext(std::shared_ptr<FDockContext> value) &
    {
        _widget->setContext(std::move(value));
        return *this;
    }

    [[nodiscard]] UIDockSpaceWidgetBuilder&& setContext(std::shared_ptr<FDockContext> value) &&
    {
        _widget->setContext(std::move(value));
        return std::move(*this);
    }
};

/// Full-screen popup overlay (menu / modal / dialog shell). Content children
/// are laid out at `_contentPos` with their desired size; the overlay itself
/// attaches to the tree's Popup layer via open(), not through ui::build.
class UIPopupOverlayWidgetBuilder final : public TUIWidgetChildrenBuilder<UIPopupOverlay, UIPopupOverlayWidgetBuilder>
{
  public:
    // Popup content layout is popup-owned at runtime (resolveContentSlotArgs +
    // canvas arrange). Generic child layout attachments would promise author
    // control that the popup later overwrites, so declarative popup content is
    // limited to bare child insertion for now.

    explicit UIPopupOverlayWidgetBuilder(std::string key, std::string displayName = {})
        : TUIWidgetChildrenBuilder(kTypeIdPopupOverlay, std::move(key), std::move(displayName))
    {
    }

    [[nodiscard]] UIPopupOverlayWidgetBuilder& setRole(UIPopupOverlay::EOverlayRole value) &
    {
        _widget->setRole(value);
        return *this;
    }

    [[nodiscard]] UIPopupOverlayWidgetBuilder&& setRole(UIPopupOverlay::EOverlayRole value) &&
    {
        _widget->setRole(value);
        return std::move(*this);
    }

    [[nodiscard]] UIPopupOverlayWidgetBuilder& setOnDismiss(std::function<void()> value) &
    {
        _widget->_onDismiss = std::move(value);
        return *this;
    }

    [[nodiscard]] UIPopupOverlayWidgetBuilder&& setOnDismiss(std::function<void()> value) &&
    {
        _widget->_onDismiss = std::move(value);
        return std::move(*this);
    }
};

} // namespace ya::ui
