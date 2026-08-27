#pragma once

// Native retained DSL authoring surface. These helpers build live widgets
// directly into the runtime tree; future document/component adapters may
// project to the same kernel without sharing this builder API.

#include "GUI/Declarative/ControlBuilders.h"
#include "GUI/Declarative/LayoutBuilders.h"

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

template<UIWidgetBuilder TBuilder>
UIElementRef build(WidgetTree& tree, UIElement& parent, TBuilder&& builder)
{
    UIElementRef root = std::forward<TBuilder>(builder).release();
    YA_CORE_ASSERT(root, "ui::build: empty root");
    const WidgetAttachment attached = tree.attach(parent, root);
    YA_CORE_ASSERT(attached.valid(), "ui::build: attach failed for '{}'", root->_name);
    return root;
}

} // namespace ya::ui
