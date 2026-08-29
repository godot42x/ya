#pragma once

// Native retained DSL authoring surface. These helpers build live widgets
// directly into the runtime tree; future document/component adapters may
// project to the same kernel without sharing this builder API.

#include "GUI/Declarative/ControlBuilders.h"
#include "GUI/Declarative/LayoutBuilders.h"
#include "GUI/Declarative/ShellBuilders.h"
#include "GUI/Declarative/SlotBuilders.h"

namespace ya::ui
{

[[nodiscard]] inline UITextWidgetBuilder text(std::string key, std::string displayName = {})
{
    return UITextWidgetBuilder{std::move(key), std::move(displayName)};
}

[[nodiscard]] inline UIButtonWidgetBuilder button(std::string key, std::string displayName = {})
{
    return UIButtonWidgetBuilder{std::move(key), std::move(displayName)};
}

[[nodiscard]] inline UIPanelWidgetBuilder panel(std::string key, std::string displayName = {})
{
    return UIPanelWidgetBuilder{std::move(key), std::move(displayName)};
}

/// A canvas host: the same anchor layout a panel carries, but without a panel's
/// own visuals (no background, no corner radius). Canvas is a LAYOUT TYPE, so it
/// is available as its own host rather than only as a panel's behaviour.
[[nodiscard]] inline UIPanelWidgetBuilder canvas(std::string key, std::string displayName = {})
{
    return panel(std::move(key), std::move(displayName)).setStyleKey("canvas");
}

[[nodiscard]] inline UIContainerWidgetBuilder column(std::string key, std::string displayName = {})
{
    return UIContainerWidgetBuilder{std::move(key), std::move(displayName), EWidgetBoxLayout::Vertical};
}

[[nodiscard]] inline UIContainerWidgetBuilder row(std::string key, std::string displayName = {})
{
    return UIContainerWidgetBuilder{std::move(key), std::move(displayName), EWidgetBoxLayout::Horizontal};
}

[[nodiscard]] inline UITextFieldWidgetBuilder textField(std::string key, std::string displayName = {})
{
    return UITextFieldWidgetBuilder{std::move(key), std::move(displayName)};
}

[[nodiscard]] inline UICheckBoxWidgetBuilder checkBox(std::string key, std::string displayName = {})
{
    return UICheckBoxWidgetBuilder{std::move(key), std::move(displayName)};
}

[[nodiscard]] inline UISliderWidgetBuilder slider(std::string key, std::string displayName = {})
{
    return UISliderWidgetBuilder{std::move(key), std::move(displayName)};
}

[[nodiscard]] inline UISelectableRowWidgetBuilder selectableRow(std::string key, std::string displayName = {})
{
    return UISelectableRowWidgetBuilder{std::move(key), std::move(displayName)};
}

[[nodiscard]] inline UIComboBoxWidgetBuilder comboBox(std::string key, std::string displayName = {})
{
    return UIComboBoxWidgetBuilder{std::move(key), std::move(displayName)};
}

[[nodiscard]] inline UIImageWidgetBuilder image(std::string key, std::string displayName = {})
{
    return UIImageWidgetBuilder{std::move(key), std::move(displayName)};
}

[[nodiscard]] inline UISplitPaneWidgetBuilder splitPane(std::string key, std::string displayName = {})
{
    return UISplitPaneWidgetBuilder{std::move(key), std::move(displayName)};
}

[[nodiscard]] inline UIScrollViewportWidgetBuilder scroll(std::string key, std::string displayName = {})
{
    return UIScrollViewportWidgetBuilder{std::move(key), std::move(displayName)};
}

[[nodiscard]] inline UIOverlayWidgetBuilder overlay(std::string key, std::string displayName = {})
{
    return UIOverlayWidgetBuilder{std::move(key), std::move(displayName)};
}

[[nodiscard]] inline UISizeBoxWidgetBuilder sizeBox(std::string key, std::string displayName = {})
{
    return UISizeBoxWidgetBuilder{std::move(key), std::move(displayName)};
}

[[nodiscard]] inline UIMenuBarWidgetBuilder menuBar(std::string key, std::string displayName = {})
{
    return UIMenuBarWidgetBuilder{std::move(key), std::move(displayName)};
}

[[nodiscard]] inline UITreeViewWidgetBuilder treeView(std::string key, std::string displayName = {})
{
    return UITreeViewWidgetBuilder{std::move(key), std::move(displayName)};
}

[[nodiscard]] inline UIDockSpaceWidgetBuilder dockSpace(std::string key, std::string displayName = {})
{
    return UIDockSpaceWidgetBuilder{std::move(key), std::move(displayName)};
}

[[nodiscard]] inline UIPopupOverlayWidgetBuilder popupOverlay(std::string key, std::string displayName = {})
{
    return UIPopupOverlayWidgetBuilder{std::move(key), std::move(displayName)};
}

template<UIWidgetBuilder TBuilder>
UIElementRef build(WidgetTree& tree, UIElement& parent, TBuilder&& builder)
{
    UIElementRef root = std::forward<TBuilder>(builder).release();
    YA_CORE_ASSERT(root, "ui::build: empty root");
    const WidgetAttachment attached = tree.attach(parent, root);
    YA_CORE_ASSERT(attached.valid(), "ui::build: attach failed for '{}'", root->_name);
    return root;
}

/// Path-B (anchor-owning) parent variant: the child's stretch geometry is carried
/// on the parent->child slot edge, never authored on the child.
template<UIWidgetBuilder TBuilder>
UIElementRef build(WidgetTree& tree, UIElement& parent, TBuilder&& builder, const FCanvasPanelSlotArgs& slot)
{
    UIElementRef root = std::forward<TBuilder>(builder).release();
    YA_CORE_ASSERT(root, "ui::build: empty root");
    const WidgetAttachment attached = tree.attach(parent, root);
    YA_CORE_ASSERT(attached.valid(), "ui::build: attach failed for '{}'", root->_name);
    if (auto* s = parent.getSlotForChild(*root)) {
        if (auto* canvas = dynamic_cast<UICanvasPanelSlot*>(s)) {
            canvas->apply(slot);
        }
    }
    return root;
}

/// Same as build(), but keeps the concrete widget type so the host can retain
/// a typed shared_ptr for later sync (instead of casting the base ref back).
template<typename TWidget, typename TBuilder>
std::shared_ptr<TWidget> buildAs(WidgetTree& tree, UIElement& parent, TBuilder&& builder)
{
    auto widget = std::dynamic_pointer_cast<TWidget>(std::forward<TBuilder>(builder).release());
    YA_CORE_ASSERT(widget, "ui::buildAs: builder produced the wrong widget class");
    const WidgetAttachment attached = tree.attach(parent, widget);
    YA_CORE_ASSERT(attached.valid(), "ui::buildAs: attach failed for '{}'", widget->_name);
    return widget;
}

template<typename TWidget, typename TBuilder>
std::shared_ptr<TWidget> buildAs(WidgetTree& tree, UIElement& parent, TBuilder&& builder, const FCanvasPanelSlotArgs& slot)
{
    auto widget = std::dynamic_pointer_cast<TWidget>(std::forward<TBuilder>(builder).release());
    YA_CORE_ASSERT(widget, "ui::buildAs: builder produced the wrong widget class");
    const WidgetAttachment attached = tree.attach(parent, widget);
    YA_CORE_ASSERT(attached.valid(), "ui::buildAs: attach failed for '{}'", widget->_name);
    if (auto* s = parent.getSlotForChild(*widget)) {
        if (auto* canvas = dynamic_cast<UICanvasPanelSlot*>(s)) {
            canvas->apply(slot);
        }
    }
    return widget;
}

/// Apply a canvas slot to an already-attached child (e.g. a root attached via
/// attachToLayer that still needs canvas stretch geometry).
inline void attachCanvasSlot(UIElement& parent, UIElement& child, const FCanvasSlotArgs& slot)
{
    if (auto* s = parent.getSlotForChild(child)) {
        if (auto* canvas = dynamic_cast<UICanvasSlot*>(s)) {
            canvas->apply(slot);
        }
    }
}

/// Build with a unified layout spec: the host consumes the capabilities it
/// implements. This is the shared entry point for DSL-authored and
/// imperatively-attached children alike.
template<UIWidgetBuilder TBuilder>
UIElementRef build(WidgetTree& tree, UIElement& parent, TBuilder&& builder, const FUILayoutSpec& spec)
{
    UIElementRef root = std::forward<TBuilder>(builder).release();
    YA_CORE_ASSERT(root, "ui::build: empty root");
    const WidgetAttachment attached = tree.attach(parent, root);
    YA_CORE_ASSERT(attached.valid(), "ui::build: attach failed for '{}'", root->_name);
    if (UISlot* s = parent.getSlotForChild(*root)) {
        applyLayoutSpecToSlot(*s, *root, spec);
    }
    return root;
}

template<typename TWidget, typename TBuilder>
std::shared_ptr<TWidget> buildAs(WidgetTree& tree, UIElement& parent, TBuilder&& builder, const FUILayoutSpec& spec)
{
    auto widget = std::dynamic_pointer_cast<TWidget>(std::forward<TBuilder>(builder).release());
    YA_CORE_ASSERT(widget, "ui::buildAs: builder produced the wrong widget class");
    const WidgetAttachment attached = tree.attach(parent, widget);
    YA_CORE_ASSERT(attached.valid(), "ui::buildAs: attach failed for '{}'", widget->_name);
    if (UISlot* s = parent.getSlotForChild(*widget)) {
        applyLayoutSpecToSlot(*s, *widget, spec);
    }
    return widget;
}

/// Apply a unified layout spec to an already-attached child.
inline void attachLayout(UIElement& parent, UIElement& child, const FUILayoutSpec& spec)
{
    if (UISlot* s = parent.getSlotForChild(child)) {
        applyLayoutSpecToSlot(*s, child, spec);
    }
}

} // namespace ya::ui
