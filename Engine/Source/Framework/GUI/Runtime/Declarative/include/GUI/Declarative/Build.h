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

/// Group children without adding a runtime widget. The parent builder still
/// owns and type-checks every child edge, including its SlotArgs.
template<typename... TItems>
[[nodiscard]] auto fragment(TItems&&... items)
{
    return TUIChildrenFragment<std::decay_t<TItems>...>(std::forward<TItems>(items)...);
}

/// Readable alias for a fragment used to name a local group in editor pages.
template<typename... TItems>
[[nodiscard]] auto group(TItems&&... items)
{
    return fragment(std::forward<TItems>(items)...);
}

template<UIWidgetBuilder TItem>
[[nodiscard]] auto when(bool condition, TItem&& item)
{
    return TUIConditionalChild<std::decay_t<TItem>>(condition, std::forward<TItem>(item));
}

template<UIWidgetBuilder TItem>
[[nodiscard]] auto unless(bool condition, TItem&& item)
{
    return when(!condition, std::forward<TItem>(item));
}

template<UIWidgetBuilder TThen, UIWidgetBuilder TElse>
[[nodiscard]] auto ifElse(bool condition, TThen&& thenItem, TElse&& elseItem)
{
    return TUIIfElseChild<std::decay_t<TThen>, std::decay_t<TElse>>(
        condition, std::forward<TThen>(thenItem), std::forward<TElse>(elseItem));
}

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
        "{}: child '{}' attached to canvas host '{}' without explicit slot intent; using the default top-left Auto/Auto canvas slot. Use canvasSlot().fill()/size(), or spanning anchor()/insets() (those axes stretch). Bare Auto + 0 desired is invisible.",
        apiName,
        child._name,
        parent._name);
}

template<typename TArgs>
inline void applySlotArgs(UIElement& parent, UIElement& child, const TArgs& args)
{
    parent.initializeChildSlot(child, [&args, &parent, &child](UIElement&, UISlot& slot) {
        const bool applied = slot.applyArgs(args);
        YA_CORE_ASSERT(applied,
                       "ui::attach: parent '{}' does not accept slot args for child '{}'",
                       parent._name,
                       child._name);
    });
}

template<UISlotBuilder TSlotBuilder>
    requires requires(const std::remove_reference_t<TSlotBuilder>& builder) { builder.args(); }
inline void applyAttachedSlot(UIElement& parent, UIElement& child, TSlotBuilder&& slotBuilder)
{
    applySlotArgs(parent, child, slotBuilder.args());
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

[[nodiscard]] inline UICanvasPanelWidgetBuilder canvasPanel(std::string key, std::string displayName = {})
{
    return UICanvasPanelWidgetBuilder{std::move(key), std::move(displayName)};
}
YA_UI_ANONYMOUS_FACTORY(canvasPanel, UICanvasPanelWidgetBuilder)

[[nodiscard]] inline UIBorderWidgetBuilder border(std::string key, std::string displayName = {})
{
    return UIBorderWidgetBuilder{std::move(key), std::move(displayName)};
}
YA_UI_ANONYMOUS_FACTORY(border, UIBorderWidgetBuilder)

[[nodiscard]] inline UIContainerWidgetBuilder column(std::string key, std::string displayName = {})
{
    return UIContainerWidgetBuilder{std::move(key),
                                    std::move(displayName),
                                    EWidgetBoxLayout::Vertical};
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

[[nodiscard]] inline UIExpanderWidgetBuilder treeNode(std::string key, std::string displayName = {})
{
    return UIExpanderWidgetBuilder{std::move(key), std::move(displayName)};
}
YA_UI_ANONYMOUS_FACTORY(treeNode, UIExpanderWidgetBuilder)

/// ImGui `CollapsingHeader`: the same `UIExpander` / `treeNode` with Framed.
[[nodiscard]] inline UIExpanderWidgetBuilder collapsingHeader(std::string key, std::string displayName = {})
{
    return treeNode(std::move(key), std::move(displayName)).setFramed(true);
}
[[nodiscard]] inline UIExpanderWidgetBuilder collapsingHeader()
{
    return treeNode().setFramed(true);
}

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

/// Mount an already-built widget into the live tree using the host's implicit
/// default slot intent. Builders stay in the authoring phase until the caller
/// explicitly `.release()` / `.share()`s them.
inline WidgetAttachment attach(WidgetTree& tree, UIElement& parent, const UIElementRef& widget)
{
    YA_CORE_ASSERT(widget, "ui::attach: empty widget");
    const WidgetAttachment attached = tree.attach(parent, widget);
    YA_CORE_ASSERT(attached.valid(), "ui::attach: attach failed for '{}'", widget ? widget->_name : "<null>");
    if (attached.valid() && widget) {
        detail::exposeImplicitCanvasBuild("ui::attach", parent, *widget);
    }
    return attached;
}

/// Mount an already-built widget and apply parent-owned slot args. The parent
/// type's `SlotArgs` selects canvas / box / overlay / content — passing `UIElement&`
/// here is a compile error so the slot kind cannot be lost.
template<typename TParent>
    requires requires { typename TParent::SlotArgs; }
WidgetAttachment attach(WidgetTree& tree,
                       TParent& parent,
                       const UIElementRef& widget,
                       const typename TParent::SlotArgs& slot)
{
    YA_CORE_ASSERT(widget, "ui::attach: empty widget");
    const WidgetAttachment attached = tree.attach(parent, widget);
    YA_CORE_ASSERT(attached.valid(), "ui::attach: attach failed for '{}'", widget ? widget->_name : "<null>");
    if (attached.valid() && widget) {
        detail::applySlotArgs(parent, *widget, slot);
    }
    return attached;
}

/// Same as the SlotArgs overload, with a fluent slot builder. The builder type
/// must match `TParent::SlotArgs` (`SlotBuilderAcceptedBy`).
template<typename TParent, UISlotBuilder TSlotBuilder>
    requires SlotBuilderAcceptedBy<TParent, TSlotBuilder>
WidgetAttachment attach(WidgetTree& tree, TParent& parent, const UIElementRef& widget, TSlotBuilder&& slotBuilder)
{
    return attach(tree, parent, widget, slotBuilder.args());
}

/// Reset the typed layout intent on an already-existing parent->child slot
/// edge. Prefer ui::attach() when creating the edge for the first time.
template<UISlotBuilder TSlotBuilder>
inline void resetSlot(UIElement& parent, UIElement& child, TSlotBuilder&& slotBuilder)
{
    detail::applyAttachedSlot(parent, child, std::forward<TSlotBuilder>(slotBuilder));
}

} // namespace ya::ui
