#pragma once

#include "GUI/Declarative/BuilderBase.h"

#include "GUI/Widgets/Controls/Container.h"
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
};

class UIContainerWidgetBuilder final : public TUIWidgetChildrenBuilder<UIContainer, UIContainerWidgetBuilder>
{
  public:
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

    UIContainerWidgetBuilder& childFill(UIElementRef node) &
    {
        applyChildSlot(std::move(node), FBoxSlotArgs{.sizeRule = EUIBoxSlotSizeRule::Fill});
        return *this;
    }

    UIContainerWidgetBuilder&& childFill(UIElementRef node) &&
    {
        applyChildSlot(std::move(node), FBoxSlotArgs{.sizeRule = EUIBoxSlotSizeRule::Fill});
        return std::move(*this);
    }

    template<UIWidgetBuilder TChild>
    UIContainerWidgetBuilder& childFill(TChild&& builder) &
    {
        applyChildSlot(std::forward<TChild>(builder).release(),
                       FBoxSlotArgs{.sizeRule = EUIBoxSlotSizeRule::Fill});
        return *this;
    }

    template<UIWidgetBuilder TChild>
    UIContainerWidgetBuilder&& childFill(TChild&& builder) &&
    {
        applyChildSlot(std::forward<TChild>(builder).release(),
                       FBoxSlotArgs{.sizeRule = EUIBoxSlotSizeRule::Fill});
        return std::move(*this);
    }

  private:
    void applyChildSlot(UIElementRef node, const FBoxSlotArgs& slot)
    {
        UIElement* live = node.get();
        this->_widget->addDetachedChild(std::move(node));
        if (live) {
            if (auto* boxSlot = this->_widget->getBoxSlot(*live)) {
                boxSlot->apply(slot);
            }
        }
    }
};

class UISplitPaneWidgetBuilder final : public TUIWidgetChildrenBuilder<UISplitPane, UISplitPaneWidgetBuilder>
{
  public:
    explicit UISplitPaneWidgetBuilder(std::string key, std::string displayName = {})
        : TUIWidgetChildrenBuilder(kTypeIdSplitPane, std::move(key), std::move(displayName))
    {
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
    explicit UIScrollViewportWidgetBuilder(std::string key, std::string displayName = {})
        : TUIWidgetChildrenBuilder(kTypeIdScrollViewport, std::move(key), std::move(displayName))
    {
    }
};

class UIOverlayWidgetBuilder final : public TUIWidgetChildrenBuilder<UIOverlay, UIOverlayWidgetBuilder>
{
  public:
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
        UIElement* live = node.get();
        this->_widget->addDetachedChild(std::move(node));
        if (live) {
            if (auto* overlaySlot = this->_widget->getOverlaySlot(*live)) {
                overlaySlot->apply(slot);
            }
        }
    }
};

class UISizeBoxWidgetBuilder final : public TUIWidgetChildrenBuilder<UISizeBox, UISizeBoxWidgetBuilder>
{
  public:
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
};

} // namespace ya::ui
