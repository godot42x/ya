#pragma once

#include "GUI/Declarative/BuilderBase.h"

#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/Expander.h"
#include "GUI/Widgets/Controls/Overlay.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/ScrollViewport.h"
#include "GUI/Widgets/Controls/SizeBox.h"
#include "GUI/Widgets/Controls/SplitPane.h"

namespace ya::ui
{

class UIPanelWidgetBuilder final : public TUIWidgetChildrenBuilder<UIPanel, UIPanelWidgetBuilder>
{
  public:
    /// A panel carries canvas layout through its parent-owned Canvas slot.
    using SlotArgs = FCanvasSlotArgs;

    explicit UIPanelWidgetBuilder(std::string key, std::string displayName = {})
        : TUIWidgetChildrenBuilder(kTypeIdPanel, std::move(key), std::move(displayName))
    {
    }

    [[nodiscard]] UIPanelWidgetBuilder& setColor(const glm::vec4& value) &
    {
        _widget->setColor(value);
        return *this;
    }

    [[nodiscard]] UIPanelWidgetBuilder&& setColor(const glm::vec4& value) &&
    {
        _widget->setColor(value);
        return std::move(*this);
    }

    [[nodiscard]] UIPanelWidgetBuilder& setCornerRadius(float value) &
    {
        _widget->setCornerRadius(value);
        return *this;
    }

    [[nodiscard]] UIPanelWidgetBuilder&& setCornerRadius(float value) &&
    {
        _widget->setCornerRadius(value);
        return std::move(*this);
    }

    // Canvas host: child rect is resolved from the canvas slot edge.
    // A bare child(node) now means the host default canvas slot itself
    // (top-left anchor, Auto/Auto size), so child()/children()/operator[] all
    // share one consistent default-layout surface.
    using TUIWidgetChildrenBuilder::child;

    UIPanelWidgetBuilder& child(UIElementRef node, const FCanvasSlotArgs& slot) &
    {
        applyCanvasSlot(std::move(node), slot);
        return *this;
    }

    UIPanelWidgetBuilder&& child(UIElementRef node, const FCanvasSlotArgs& slot) &&
    {
        applyCanvasSlot(std::move(node), slot);
        return std::move(*this);
    }

    template<UIWidgetBuilder TChild>
    UIPanelWidgetBuilder& child(TChild&& builder, const FCanvasSlotArgs& slot) &
    {
        applyCanvasSlot(std::forward<TChild>(builder).release(), slot);
        return *this;
    }

    template<UIWidgetBuilder TChild>
    UIPanelWidgetBuilder&& child(TChild&& builder, const FCanvasSlotArgs& slot) &&
    {
        applyCanvasSlot(std::forward<TChild>(builder).release(), slot);
        return std::move(*this);
    }
};

class UIContainerWidgetBuilder final : public TUIWidgetChildrenBuilder<UIContainer, UIContainerWidgetBuilder>
{
  public:
    /// Box hosts share space along the main axis through Box slots.
    using SlotArgs = FBoxSlotArgs;

    explicit UIContainerWidgetBuilder(std::string key, std::string displayName = {},
                                      EWidgetBoxLayout direction = EWidgetBoxLayout::Vertical)
        : TUIWidgetChildrenBuilder(kTypeIdContainer, std::move(key), std::move(displayName))
    {
        _widget->setDirection(direction);
    }

    [[nodiscard]] UIContainerWidgetBuilder& setDirection(EWidgetBoxLayout value) &
    {
        _widget->setDirection(value);
        return *this;
    }

    [[nodiscard]] UIContainerWidgetBuilder&& setDirection(EWidgetBoxLayout value) &&
    {
        _widget->setDirection(value);
        return std::move(*this);
    }

    [[nodiscard]] UIContainerWidgetBuilder& setSpacing(float value) &
    {
        _widget->setSpacing(value);
        return *this;
    }

    [[nodiscard]] UIContainerWidgetBuilder&& setSpacing(float value) &&
    {
        _widget->setSpacing(value);
        return std::move(*this);
    }

    [[nodiscard]] UIContainerWidgetBuilder& setPadding(const glm::vec2& value) &
    {
        _widget->setPadding(value);
        return *this;
    }

    [[nodiscard]] UIContainerWidgetBuilder&& setPadding(const glm::vec2& value) &&
    {
        _widget->setPadding(value);
        return std::move(*this);
    }

    [[nodiscard]] UIContainerWidgetBuilder& setClipChildren(bool value) &
    {
        _widget->setClipChildren(value);
        return *this;
    }

    [[nodiscard]] UIContainerWidgetBuilder&& setClipChildren(bool value) &&
    {
        _widget->setClipChildren(value);
        return std::move(*this);
    }

    [[nodiscard]] UIContainerWidgetBuilder& setMainAxisAlignment(EWidgetMainAxisAlignment value) &
    {
        _widget->setMainAxisAlignment(value);
        return *this;
    }

    [[nodiscard]] UIContainerWidgetBuilder&& setMainAxisAlignment(EWidgetMainAxisAlignment value) &&
    {
        _widget->setMainAxisAlignment(value);
        return std::move(*this);
    }

    [[nodiscard]] UIContainerWidgetBuilder& setStretchLastChild(bool value) &
    {
        _widget->setStretchLastChild(value);
        return *this;
    }

    [[nodiscard]] UIContainerWidgetBuilder&& setStretchLastChild(bool value) &&
    {
        _widget->setStretchLastChild(value);
        return std::move(*this);
    }

    using TUIWidgetChildrenBuilder::child;

    UIContainerWidgetBuilder& child(UIElementRef node, const FBoxSlotArgs& slot) &
    {
        applyChildSlot(std::move(node), slot);
        return *this;
    }

    UIContainerWidgetBuilder&& child(UIElementRef node, const FBoxSlotArgs& slot) &&
    {
        applyChildSlot(std::move(node), slot);
        return std::move(*this);
    }

    template<UIWidgetBuilder TChild>
    UIContainerWidgetBuilder& child(TChild&& builder, const FBoxSlotArgs& slot) &
    {
        applyChildSlot(std::forward<TChild>(builder).release(), slot);
        return *this;
    }

    template<UIWidgetBuilder TChild>
    UIContainerWidgetBuilder&& child(TChild&& builder, const FBoxSlotArgs& slot) &&
    {
        applyChildSlot(std::forward<TChild>(builder).release(), slot);
        return std::move(*this);
    }

  private:
    void applyChildSlot(UIElementRef node, const FBoxSlotArgs& slot)
    {
        this->attachChild(std::move(node), [&slot](UIElement&, UISlot& childSlot) {
            if (auto* boxSlot = childSlot.as<UIBoxSlot>()) {
                boxSlot->apply(slot);
            }
        });
    }
};

class UISplitPaneWidgetBuilder final : public TUIWidgetChildrenBuilder<UISplitPane, UISplitPaneWidgetBuilder>
{
  public:
    /// Split hosts position two panes by ratio.
    using SlotArgs = FOverlaySlotArgs;

    explicit UISplitPaneWidgetBuilder(std::string key, std::string displayName = {})
        : TUIWidgetChildrenBuilder(kTypeIdSplitPane, std::move(key), std::move(displayName))
    {
    }

    // The split owns the main axis via the ratio, so a pane's only intent is
    // its cross-axis placement. The using keeps plain child(node) visible.
    using TUIWidgetChildrenBuilder::child;

    UISplitPaneWidgetBuilder& child(UIElementRef node, const FOverlaySlotArgs& slot) &
    {
        this->applySingleChildSlot(std::move(node), slot);
        return *this;
    }
    UISplitPaneWidgetBuilder&& child(UIElementRef node, const FOverlaySlotArgs& slot) &&
    {
        this->applySingleChildSlot(std::move(node), slot);
        return std::move(*this);
    }

    template<UIWidgetBuilder TChild>
    UISplitPaneWidgetBuilder& child(TChild&& builder, const FOverlaySlotArgs& slot) &
    {
        this->applySingleChildSlot(std::forward<TChild>(builder).release(), slot);
        return *this;
    }
    template<UIWidgetBuilder TChild>
    UISplitPaneWidgetBuilder&& child(TChild&& builder, const FOverlaySlotArgs& slot) &&
    {
        this->applySingleChildSlot(std::forward<TChild>(builder).release(), slot);
        return std::move(*this);
    }

    [[nodiscard]] UISplitPaneWidgetBuilder& setOrientation(ESplitOrientation value) &
    {
        _widget->setOrientation(value);
        return *this;
    }

    [[nodiscard]] UISplitPaneWidgetBuilder&& setOrientation(ESplitOrientation value) &&
    {
        _widget->setOrientation(value);
        return std::move(*this);
    }

    [[nodiscard]] UISplitPaneWidgetBuilder& setSplitRatio(float value) &
    {
        _widget->setSplitRatio(value);
        return *this;
    }

    [[nodiscard]] UISplitPaneWidgetBuilder&& setSplitRatio(float value) &&
    {
        _widget->setSplitRatio(value);
        return std::move(*this);
    }

    [[nodiscard]] UISplitPaneWidgetBuilder& setMinFirstExtent(float value) &
    {
        _widget->setMinFirstExtent(value);
        return *this;
    }

    [[nodiscard]] UISplitPaneWidgetBuilder&& setMinFirstExtent(float value) &&
    {
        _widget->setMinFirstExtent(value);
        return std::move(*this);
    }

    [[nodiscard]] UISplitPaneWidgetBuilder& setMinSecondExtent(float value) &
    {
        _widget->setMinSecondExtent(value);
        return *this;
    }

    [[nodiscard]] UISplitPaneWidgetBuilder&& setMinSecondExtent(float value) &&
    {
        _widget->setMinSecondExtent(value);
        return std::move(*this);
    }

    [[nodiscard]] UISplitPaneWidgetBuilder& setPadding(glm::vec2 value) &
    {
        _widget->setPadding(value);
        return *this;
    }

    [[nodiscard]] UISplitPaneWidgetBuilder&& setPadding(glm::vec2 value) &&
    {
        _widget->setPadding(value);
        return std::move(*this);
    }

    [[nodiscard]] UISplitPaneWidgetBuilder& bindSplitRatio(std::shared_ptr<Reactive<float>> ref) &
    {
        _widget->bindSplitRatio(std::move(ref));
        return *this;
    }

    [[nodiscard]] UISplitPaneWidgetBuilder&& bindSplitRatio(std::shared_ptr<Reactive<float>> ref) &&
    {
        _widget->bindSplitRatio(std::move(ref));
        return std::move(*this);
    }
};

class UIScrollViewportWidgetBuilder final : public TUIWidgetChildrenBuilder<UIScrollViewport, UIScrollViewportWidgetBuilder>
{
  public:
    /// Single-child host: the content fills the viewport.
    using SlotArgs = FOverlaySlotArgs;

    explicit UIScrollViewportWidgetBuilder(std::string key, std::string displayName = {})
        : TUIWidgetChildrenBuilder(kTypeIdScrollViewport, std::move(key), std::move(displayName))
    {
    }

    [[nodiscard]] UIScrollViewportWidgetBuilder& setAxis(EScrollAxis value) &
    {
        _widget->setAxis(value);
        return *this;
    }

    [[nodiscard]] UIScrollViewportWidgetBuilder&& setAxis(EScrollAxis value) &&
    {
        _widget->setAxis(value);
        return std::move(*this);
    }

    // A scroll viewport owns both axes; child intent is expressed by its Overlay slot.
    // The using keeps the plain child(node) overloads visible, since declaring
    // any child() here would otherwise hide the base set.
    using TUIWidgetChildrenBuilder::child;

    UIScrollViewportWidgetBuilder& child(UIElementRef node, const FOverlaySlotArgs& slot) &
    {
        this->applySingleChildSlot(std::move(node), slot);
        return *this;
    }
    UIScrollViewportWidgetBuilder&& child(UIElementRef node, const FOverlaySlotArgs& slot) &&
    {
        this->applySingleChildSlot(std::move(node), slot);
        return std::move(*this);
    }

    template<UIWidgetBuilder TChild>
    UIScrollViewportWidgetBuilder& child(TChild&& builder, const FOverlaySlotArgs& slot) &
    {
        this->applySingleChildSlot(std::forward<TChild>(builder).release(), slot);
        return *this;
    }
    template<UIWidgetBuilder TChild>
    UIScrollViewportWidgetBuilder&& child(TChild&& builder, const FOverlaySlotArgs& slot) &&
    {
        this->applySingleChildSlot(std::forward<TChild>(builder).release(), slot);
        return std::move(*this);
    }
};

class UIOverlayWidgetBuilder final : public TUIWidgetChildrenBuilder<UIOverlay, UIOverlayWidgetBuilder>
{
  public:
    /// Overlay host: layered children positioned by alignment.
    using SlotArgs = FOverlaySlotArgs;

    explicit UIOverlayWidgetBuilder(std::string key, std::string displayName = {})
        : TUIWidgetChildrenBuilder(kTypeIdOverlay, std::move(key), std::move(displayName))
    {
    }

    using TUIWidgetChildrenBuilder::child;

    UIOverlayWidgetBuilder& child(UIElementRef node, const FOverlaySlotArgs& slot) &
    {
        applyChildSlot(std::move(node), slot);
        return *this;
    }

    UIOverlayWidgetBuilder&& child(UIElementRef node, const FOverlaySlotArgs& slot) &&
    {
        applyChildSlot(std::move(node), slot);
        return std::move(*this);
    }

    template<UIWidgetBuilder TChild>
    UIOverlayWidgetBuilder& child(TChild&& builder, const FOverlaySlotArgs& slot) &
    {
        applyChildSlot(std::forward<TChild>(builder).release(), slot);
        return *this;
    }

    template<UIWidgetBuilder TChild>
    UIOverlayWidgetBuilder&& child(TChild&& builder, const FOverlaySlotArgs& slot) &&
    {
        applyChildSlot(std::forward<TChild>(builder).release(), slot);
        return std::move(*this);
    }

  private:
    void applyChildSlot(UIElementRef node, const FOverlaySlotArgs& slot)
    {
        this->attachChild(std::move(node), [&slot](UIElement&, UISlot& childSlot) {
            if (auto* overlaySlot = childSlot.as<UIOverlaySlot>()) {
                overlaySlot->apply(slot);
            }
        });
    }
};

class UISizeBoxWidgetBuilder final : public TUIWidgetChildrenBuilder<UISizeBox, UISizeBoxWidgetBuilder>
{
  public:
    /// Single-child host: the child fills the box.
    using SlotArgs = FOverlaySlotArgs;

    explicit UISizeBoxWidgetBuilder(std::string key, std::string displayName = {})
        : TUIWidgetChildrenBuilder(kTypeIdSizeBox, std::move(key), std::move(displayName))
    {
    }

    [[nodiscard]] UISizeBoxWidgetBuilder& setPadding(FMargin value) &
    {
        _widget->setPadding(value);
        return *this;
    }

    [[nodiscard]] UISizeBoxWidgetBuilder&& setPadding(FMargin value) &&
    {
        _widget->setPadding(value);
        return std::move(*this);
    }

    [[nodiscard]] UISizeBoxWidgetBuilder& setPadding(glm::vec2 value) &
    {
        _widget->setPadding(value);
        return *this;
    }

    [[nodiscard]] UISizeBoxWidgetBuilder&& setPadding(glm::vec2 value) &&
    {
        _widget->setPadding(value);
        return std::move(*this);
    }

    [[nodiscard]] UISizeBoxWidgetBuilder& setWidth(float value) &
    {
        _widget->setWidthOverride(value);
        return *this;
    }

    [[nodiscard]] UISizeBoxWidgetBuilder&& setWidth(float value) &&
    {
        _widget->setWidthOverride(value);
        return std::move(*this);
    }

    [[nodiscard]] UISizeBoxWidgetBuilder& setHeight(float value) &
    {
        _widget->setHeightOverride(value);
        return *this;
    }

    [[nodiscard]] UISizeBoxWidgetBuilder&& setHeight(float value) &&
    {
        _widget->setHeightOverride(value);
        return std::move(*this);
    }

    [[nodiscard]] UISizeBoxWidgetBuilder& setMinSize(glm::vec2 value) &
    {
        _widget->setMinSize(value);
        return *this;
    }

    [[nodiscard]] UISizeBoxWidgetBuilder&& setMinSize(glm::vec2 value) &&
    {
        _widget->setMinSize(value);
        return std::move(*this);
    }

    [[nodiscard]] UISizeBoxWidgetBuilder& setMaxSize(glm::vec2 value) &
    {
        _widget->setMaxSize(value);
        return *this;
    }

    [[nodiscard]] UISizeBoxWidgetBuilder&& setMaxSize(glm::vec2 value) &&
    {
        _widget->setMaxSize(value);
        return std::move(*this);
    }

    // A size box owns both axes; child intent is expressed by its Overlay slot.
    // The using keeps the plain child(node) overloads visible.
    using TUIWidgetChildrenBuilder::child;

    UISizeBoxWidgetBuilder& child(UIElementRef node, const FOverlaySlotArgs& slot) &
    {
        this->applySingleChildSlot(std::move(node), slot);
        return *this;
    }
    UISizeBoxWidgetBuilder&& child(UIElementRef node, const FOverlaySlotArgs& slot) &&
    {
        this->applySingleChildSlot(std::move(node), slot);
        return std::move(*this);
    }

    template<UIWidgetBuilder TChild>
    UISizeBoxWidgetBuilder& child(TChild&& builder, const FOverlaySlotArgs& slot) &
    {
        this->applySingleChildSlot(std::forward<TChild>(builder).release(), slot);
        return *this;
    }
    template<UIWidgetBuilder TChild>
    UISizeBoxWidgetBuilder&& child(TChild&& builder, const FOverlaySlotArgs& slot) &&
    {
        this->applySingleChildSlot(std::forward<TChild>(builder).release(), slot);
        return std::move(*this);
    }
};

class UIExpanderWidgetBuilder final : public TUIWidgetChildrenBuilder<UIExpander, UIExpanderWidgetBuilder>
{
  public:
    using SlotArgs = FBoxSlotArgs;

    explicit UIExpanderWidgetBuilder(std::string key, std::string displayName = {})
        : TUIWidgetChildrenBuilder(kTypeIdExpander, std::move(key), std::move(displayName))
    {
    }

    [[nodiscard]] UIExpanderWidgetBuilder& setFramed(bool value) &
    {
        _widget->setFramed(value);
        return *this;
    }
    [[nodiscard]] UIExpanderWidgetBuilder&& setFramed(bool value) &&
    {
        _widget->setFramed(value);
        return std::move(*this);
    }

    [[nodiscard]] UIExpanderWidgetBuilder& setTitle(std::string value) &
    {
        _widget->setTitle(std::move(value));
        return *this;
    }
    [[nodiscard]] UIExpanderWidgetBuilder&& setTitle(std::string value) &&
    {
        _widget->setTitle(std::move(value));
        return std::move(*this);
    }

    [[nodiscard]] UIExpanderWidgetBuilder& setIcon(FBrush icon) &
    {
        _widget->setIcon(std::move(icon));
        return *this;
    }
    [[nodiscard]] UIExpanderWidgetBuilder&& setIcon(FBrush icon) &&
    {
        _widget->setIcon(std::move(icon));
        return std::move(*this);
    }
    [[nodiscard]] UIExpanderWidgetBuilder& setIcon(std::string assetPath) &
    {
        _widget->setIcon(FBrush::image(std::move(assetPath)));
        return *this;
    }
    [[nodiscard]] UIExpanderWidgetBuilder&& setIcon(std::string assetPath) &&
    {
        _widget->setIcon(FBrush::image(std::move(assetPath)));
        return std::move(*this);
    }

    [[nodiscard]] UIExpanderWidgetBuilder& setDisclosureKind(EDisclosureKind kind) &
    {
        _widget->setDisclosureKind(kind);
        return *this;
    }
    [[nodiscard]] UIExpanderWidgetBuilder&& setDisclosureKind(EDisclosureKind kind) &&
    {
        _widget->setDisclosureKind(kind);
        return std::move(*this);
    }
    [[nodiscard]] UIExpanderWidgetBuilder& setDisclosureGlyphs(std::string collapsed, std::string expanded) &
    {
        _widget->setDisclosureGlyphs(std::move(collapsed), std::move(expanded));
        return *this;
    }
    [[nodiscard]] UIExpanderWidgetBuilder&& setDisclosureGlyphs(std::string collapsed, std::string expanded) &&
    {
        _widget->setDisclosureGlyphs(std::move(collapsed), std::move(expanded));
        return std::move(*this);
    }
    [[nodiscard]] UIExpanderWidgetBuilder& setDisclosureImages(FBrush collapsed, FBrush expanded = {}) &
    {
        _widget->setDisclosureImages(std::move(collapsed), std::move(expanded));
        return *this;
    }
    [[nodiscard]] UIExpanderWidgetBuilder&& setDisclosureImages(FBrush collapsed, FBrush expanded = {}) &&
    {
        _widget->setDisclosureImages(std::move(collapsed), std::move(expanded));
        return std::move(*this);
    }

    [[nodiscard]] UIExpanderWidgetBuilder& setExpanded(bool value) &
    {
        _widget->setExpanded(value);
        return *this;
    }
    [[nodiscard]] UIExpanderWidgetBuilder&& setExpanded(bool value) &&
    {
        _widget->setExpanded(value);
        return std::move(*this);
    }

    [[nodiscard]] UIExpanderWidgetBuilder& setSpacing(float value) &
    {
        _widget->setSpacing(value);
        return *this;
    }
    [[nodiscard]] UIExpanderWidgetBuilder&& setSpacing(float value) &&
    {
        _widget->setSpacing(value);
        return std::move(*this);
    }

    [[nodiscard]] UIExpanderWidgetBuilder& setPadding(const glm::vec2& value) &
    {
        _widget->setPadding(value);
        return *this;
    }
    [[nodiscard]] UIExpanderWidgetBuilder&& setPadding(const glm::vec2& value) &&
    {
        _widget->setPadding(value);
        return std::move(*this);
    }

    using TUIWidgetChildrenBuilder::child;

    UIExpanderWidgetBuilder& child(UIElementRef node, const FBoxSlotArgs& slot) &
    {
        applyChildSlot(std::move(node), slot);
        return *this;
    }
    UIExpanderWidgetBuilder&& child(UIElementRef node, const FBoxSlotArgs& slot) &&
    {
        applyChildSlot(std::move(node), slot);
        return std::move(*this);
    }

    template<UIWidgetBuilder TChild>
    UIExpanderWidgetBuilder& child(TChild&& builder, const FBoxSlotArgs& slot) &
    {
        applyChildSlot(std::forward<TChild>(builder).release(), slot);
        return *this;
    }
    template<UIWidgetBuilder TChild>
    UIExpanderWidgetBuilder&& child(TChild&& builder, const FBoxSlotArgs& slot) &&
    {
        applyChildSlot(std::forward<TChild>(builder).release(), slot);
        return std::move(*this);
    }

  private:
    void applyChildSlot(UIElementRef node, const FBoxSlotArgs& slot)
    {
        this->attachChild(std::move(node), [&slot](UIElement&, UISlot& childSlot) {
            if (auto* boxSlot = childSlot.as<UIBoxSlot>()) {
                boxSlot->apply(slot);
            }
        });
    }
};

} // namespace ya::ui
