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

#define YA_UI_ANONYMOUS_FACTORY(name, builder) \
    [[nodiscard]] inline builder name() { return builder{std::string{}}; }

namespace detail
{

[[nodiscard]] inline bool isImplicitDefaultCanvasSlot(const UICanvasSlot& slot)
{
    const glm::vec2& anchorMin = slot.getAnchorMin();
    const glm::vec2& anchorMax = slot.getAnchorMax();
    const glm::vec2& offset = slot.getOffset();
    const glm::vec2& minSize = slot.getMinSize();
    const glm::vec2& maxSize = slot.getMaxSize();
    const glm::vec2& pivot = slot.getPivot();
    const glm::vec2& preferredSize = slot.getPreferredSize();
    return anchorMin.x == 0.0f && anchorMin.y == 0.0f && anchorMax.x == 0.0f && anchorMax.y == 0.0f &&
           offset.x == 0.0f && offset.y == 0.0f && minSize.x == 0.0f && minSize.y == 0.0f &&
           maxSize.x == std::numeric_limits<float>::max() && maxSize.y == std::numeric_limits<float>::max() &&
           slot.getOffsets() == FMargin{} && slot.getAlignmentH() == EWidgetAlignH::Left &&
           slot.getAlignmentV() == EWidgetAlignV::Top && slot.getWidthSizeMode() == EWidgetSizeMode::Auto &&
           slot.getHeightSizeMode() == EWidgetSizeMode::Auto && pivot.x == 0.0f && pivot.y == 0.0f &&
           preferredSize.x == 0.0f && preferredSize.y == 0.0f;
}

inline void exposeImplicitCanvasBuild(const char* apiName, UIElement& parent, UIElement& child)
{
    auto* canvas = child.getSlot() ? child.getSlot()->as<UICanvasSlot>() : nullptr;
    if (canvas == nullptr || !isImplicitDefaultCanvasSlot(*canvas)) {
        return;
    }

    YA_CORE_ERROR(
        "{}: child '{}' attached to canvas host '{}' without explicit layout intent; using the default top-left Auto/Auto canvas slot. Use ui::layout().fill()/size()/anchor(...) when stronger placement is intended.",
        apiName,
        child._name,
        parent._name);
}

} // namespace detail

[[nodiscard]] inline UITextWidgetBuilder text(std::string key, std::string displayName = {})
{
    return UITextWidgetBuilder{std::move(key), std::move(displayName)};
}
YA_UI_ANONYMOUS_FACTORY(text, UITextWidgetBuilder)

[[nodiscard]] inline UIButtonWidgetBuilder button(std::string key, std::string displayName = {})
{
    return UIButtonWidgetBuilder{std::move(key), std::move(displayName)};
}
YA_UI_ANONYMOUS_FACTORY(button, UIButtonWidgetBuilder)

[[nodiscard]] inline UIPanelWidgetBuilder panel(std::string key, std::string displayName = {})
{
    return UIPanelWidgetBuilder{std::move(key), std::move(displayName)};
}
YA_UI_ANONYMOUS_FACTORY(panel, UIPanelWidgetBuilder)

/// A canvas host: the same anchor layout a panel carries, but without a panel's
/// own visuals (no background, no corner radius). Canvas is a LAYOUT TYPE, so it
/// is available as its own host rather than only as a panel's behaviour.
[[nodiscard]] inline UIPanelWidgetBuilder canvas(std::string key, std::string displayName = {})
{
    return panel(std::move(key), std::move(displayName)).setStyleKey("canvas");
}
[[nodiscard]] inline UIPanelWidgetBuilder canvas()
{
    return panel().setStyleKey("canvas");
}

[[nodiscard]] inline UIContainerWidgetBuilder column(std::string key, std::string displayName = {})
{
    return UIContainerWidgetBuilder{std::move(key), std::move(displayName), EWidgetBoxLayout::Vertical};
}
YA_UI_ANONYMOUS_FACTORY(column, UIContainerWidgetBuilder)

[[nodiscard]] inline UIContainerWidgetBuilder row(std::string key, std::string displayName = {})
{
    return UIContainerWidgetBuilder{std::move(key), std::move(displayName), EWidgetBoxLayout::Horizontal};
}
YA_UI_ANONYMOUS_FACTORY(row, UIContainerWidgetBuilder)

[[nodiscard]] inline UITextFieldWidgetBuilder textField(std::string key, std::string displayName = {})
{
    return UITextFieldWidgetBuilder{std::move(key), std::move(displayName)};
}
YA_UI_ANONYMOUS_FACTORY(textField, UITextFieldWidgetBuilder)

[[nodiscard]] inline UICheckBoxWidgetBuilder checkBox(std::string key, std::string displayName = {})
{
    return UICheckBoxWidgetBuilder{std::move(key), std::move(displayName)};
}
YA_UI_ANONYMOUS_FACTORY(checkBox, UICheckBoxWidgetBuilder)

[[nodiscard]] inline UISliderWidgetBuilder slider(std::string key, std::string displayName = {})
{
    return UISliderWidgetBuilder{std::move(key), std::move(displayName)};
}
YA_UI_ANONYMOUS_FACTORY(slider, UISliderWidgetBuilder)

[[nodiscard]] inline UISelectableRowWidgetBuilder selectableRow(std::string key, std::string displayName = {})
{
    return UISelectableRowWidgetBuilder{std::move(key), std::move(displayName)};
}
YA_UI_ANONYMOUS_FACTORY(selectableRow, UISelectableRowWidgetBuilder)

[[nodiscard]] inline UIComboBoxWidgetBuilder comboBox(std::string key, std::string displayName = {})
{
    return UIComboBoxWidgetBuilder{std::move(key), std::move(displayName)};
}
YA_UI_ANONYMOUS_FACTORY(comboBox, UIComboBoxWidgetBuilder)

[[nodiscard]] inline UIImageWidgetBuilder image(std::string key, std::string displayName = {})
{
    return UIImageWidgetBuilder{std::move(key), std::move(displayName)};
}
YA_UI_ANONYMOUS_FACTORY(image, UIImageWidgetBuilder)

[[nodiscard]] inline UISplitPaneWidgetBuilder splitPane(std::string key, std::string displayName = {})
{
    return UISplitPaneWidgetBuilder{std::move(key), std::move(displayName)};
}
YA_UI_ANONYMOUS_FACTORY(splitPane, UISplitPaneWidgetBuilder)

[[nodiscard]] inline UIScrollViewportWidgetBuilder scroll(std::string key, std::string displayName = {})
{
    return UIScrollViewportWidgetBuilder{std::move(key), std::move(displayName)};
}
YA_UI_ANONYMOUS_FACTORY(scroll, UIScrollViewportWidgetBuilder)

[[nodiscard]] inline UIOverlayWidgetBuilder overlay(std::string key, std::string displayName = {})
{
    return UIOverlayWidgetBuilder{std::move(key), std::move(displayName)};
}
YA_UI_ANONYMOUS_FACTORY(overlay, UIOverlayWidgetBuilder)

[[nodiscard]] inline UISizeBoxWidgetBuilder sizeBox(std::string key, std::string displayName = {})
{
    return UISizeBoxWidgetBuilder{std::move(key), std::move(displayName)};
}
YA_UI_ANONYMOUS_FACTORY(sizeBox, UISizeBoxWidgetBuilder)

[[nodiscard]] inline UIMenuBarWidgetBuilder menuBar(std::string key, std::string displayName = {})
{
    return UIMenuBarWidgetBuilder{std::move(key), std::move(displayName)};
}
YA_UI_ANONYMOUS_FACTORY(menuBar, UIMenuBarWidgetBuilder)

[[nodiscard]] inline UITreeViewWidgetBuilder treeView(std::string key, std::string displayName = {})
{
    return UITreeViewWidgetBuilder{std::move(key), std::move(displayName)};
}
YA_UI_ANONYMOUS_FACTORY(treeView, UITreeViewWidgetBuilder)

[[nodiscard]] inline UIDockSpaceWidgetBuilder dockSpace(std::string key, std::string displayName = {})
{
    return UIDockSpaceWidgetBuilder{std::move(key), std::move(displayName)};
}
YA_UI_ANONYMOUS_FACTORY(dockSpace, UIDockSpaceWidgetBuilder)

[[nodiscard]] inline UIPopupOverlayWidgetBuilder popupOverlay(std::string key, std::string displayName = {})
{
    return UIPopupOverlayWidgetBuilder{std::move(key), std::move(displayName)};
}
YA_UI_ANONYMOUS_FACTORY(popupOverlay, UIPopupOverlayWidgetBuilder)

#undef YA_UI_ANONYMOUS_FACTORY

template<UIWidgetBuilder TBuilder>
UIElementRef build(WidgetTree& tree, UIElement& parent, TBuilder&& builder)
{
    UIElementRef root = std::forward<TBuilder>(builder).release();
    YA_CORE_ASSERT(root, "ui::build: empty root");
    const WidgetAttachment attached = tree.attach(parent, root);
    YA_CORE_ASSERT(attached.valid(), "ui::build: attach failed for '{}'", root->_name);
    detail::exposeImplicitCanvasBuild("ui::build", parent, *root);
    return root;
}

/// Canvas parent variant: the child's stretch geometry is carried on the
/// parent->child slot edge, never authored on the child.
template<UIWidgetBuilder TBuilder>
UIElementRef build(WidgetTree& tree, UIElement& parent, TBuilder&& builder, const FCanvasSlotArgs& slot)
{
    UIElementRef root = std::forward<TBuilder>(builder).release();
    YA_CORE_ASSERT(root, "ui::build: empty root");
    const WidgetAttachment attached = tree.attach(parent, root);
    YA_CORE_ASSERT(attached.valid(), "ui::build: attach failed for '{}'", root->_name);
    if (auto* s = parent.getSlotForChild(*root)) {
        if (auto* canvas = s ? s->template as<UICanvasSlot>() : nullptr) {
            canvas->apply(slot);
        }
    }
    return root;
}

template<UIWidgetBuilder TBuilder, UISlotBuilder TSlotBuilder>
    requires std::same_as<std::remove_cvref_t<decltype(std::declval<const std::remove_reference_t<TSlotBuilder>&>().args())>, FCanvasSlotArgs>
UIElementRef build(WidgetTree& tree, UIElement& parent, TBuilder&& builder, TSlotBuilder&& slotBuilder)
{
    return build(tree, parent, std::forward<TBuilder>(builder), slotBuilder.args());
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
    detail::exposeImplicitCanvasBuild("ui::buildAs", parent, *widget);
    return widget;
}

template<typename TWidget, typename TBuilder>
std::shared_ptr<TWidget> buildAs(WidgetTree& tree, UIElement& parent, TBuilder&& builder, const FCanvasSlotArgs& slot)
{
    auto widget = std::dynamic_pointer_cast<TWidget>(std::forward<TBuilder>(builder).release());
    YA_CORE_ASSERT(widget, "ui::buildAs: builder produced the wrong widget class");
    const WidgetAttachment attached = tree.attach(parent, widget);
    YA_CORE_ASSERT(attached.valid(), "ui::buildAs: attach failed for '{}'", widget->_name);
    if (auto* s = parent.getSlotForChild(*widget)) {
        if (auto* canvas = s ? s->template as<UICanvasSlot>() : nullptr) {
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
        if (auto* canvas = s ? s->as<UICanvasSlot>() : nullptr) {
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
    parent.initializeChildSlot(*root, [&spec](UIElement& child, UISlot& slot) {
        applyLayoutSpecToSlot(slot, child, spec);
    });
    return root;
}

template<typename TWidget, typename TBuilder>
std::shared_ptr<TWidget> buildAs(WidgetTree& tree, UIElement& parent, TBuilder&& builder, const FUILayoutSpec& spec)
{
    auto widget = std::dynamic_pointer_cast<TWidget>(std::forward<TBuilder>(builder).release());
    YA_CORE_ASSERT(widget, "ui::buildAs: builder produced the wrong widget class");
    const WidgetAttachment attached = tree.attach(parent, widget);
    YA_CORE_ASSERT(attached.valid(), "ui::buildAs: attach failed for '{}'", widget->_name);
    parent.initializeChildSlot(*widget, [&spec](UIElement& child, UISlot& slot) {
        applyLayoutSpecToSlot(slot, child, spec);
    });
    return widget;
}

/// Apply a unified layout spec to an already-attached child.
inline void attachLayout(UIElement& parent, UIElement& child, const FUILayoutSpec& spec)
{
    parent.initializeChildSlot(child, [&spec](UIElement& live, UISlot& slot) {
        applyLayoutSpecToSlot(slot, live, spec);
    });
}

template<UISlotBuilder TSlotBuilder>
    requires requires(const std::remove_reference_t<TSlotBuilder>& builder) { builder.args(); }
inline void attachSlot(UIElement& parent, UIElement& child, TSlotBuilder&& slotBuilder)
{
    using TArgs = std::remove_cvref_t<decltype(slotBuilder.args())>;
    parent.initializeChildSlot(child, [&slotBuilder](UIElement&, UISlot& slot) {
        if constexpr (std::same_as<TArgs, FCanvasSlotArgs>) {
            if (auto* typed = slot.as<UICanvasSlot>()) typed->apply(slotBuilder.args());
        }
        else if constexpr (std::same_as<TArgs, FBoxSlotArgs>) {
            if (auto* typed = slot.as<UIBoxSlot>()) typed->apply(slotBuilder.args());
        }
        else if constexpr (std::same_as<TArgs, FOverlaySlotArgs>) {
            if (auto* typed = slot.as<UIOverlaySlot>()) typed->apply(slotBuilder.args());
        }
    });
}

/// `ui::build(tree, parent, ui::layout().size({0, 22}) >> widget)`: the spec
/// lands on the parent-owned edge at attach time.
template<EUILayoutCap Caps, UIWidgetBuilder TChild>
UIElementRef build(WidgetTree& tree, UIElement& parent, TUILayoutAttachment<Caps, TChild> attachment)
{
    return build(tree, parent, std::move(attachment.child), attachment.spec);
}

} // namespace ya::ui
