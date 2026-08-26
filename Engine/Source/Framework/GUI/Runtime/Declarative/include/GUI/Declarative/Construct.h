#pragma once

// ============================================================================
// Static DSL: typed builders that materialize live widgets at Construct time.
//
// This is the default path (Slate FArguments / SNew). There is no
// UIDescription, apply hook, or reconciler. Value updates go through
// Reactive bindings on the live widget; known structure uses attach/detach.
// ============================================================================

#include "Core/Log.h"

#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/CheckBox.h"
#include "GUI/Widgets/Controls/ComboBox.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/Image.h"
#include "GUI/Widgets/Controls/Overlay.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/ScrollViewport.h"
#include "GUI/Widgets/Controls/SizeBox.h"
#include "GUI/Widgets/Controls/Slider.h"
#include "GUI/Widgets/Controls/SplitPane.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TextField.h"
#include "GUI/Widgets/CompoundWidget.h"
#include "GUI/Widgets/UITypeRegistry.h"
#include "GUI/Widgets/WidgetTree.h"

#include <type_traits>
#include <utility>

namespace ya::ui
{

template<typename T>
concept UIWidgetBuilder = requires(T&& builder) {
    { std::forward<T>(builder).release() } -> std::convertible_to<UIElementRef>;
};

template<typename T>
concept UICompoundWidgetType = std::derived_from<T, UICompoundWidget>;

template<typename TWidget>
[[nodiscard]] std::shared_ptr<TWidget> makeLiveWidget(const char* typeId,
                                                      std::string key,
                                                      std::string displayName)
{
    UIElementRef raw = UITypeRegistry::instance().createInstance(typeId);
    auto widget = std::dynamic_pointer_cast<TWidget>(raw);
    YA_CORE_ASSERT(widget, "ui::makeLiveWidget: registry type '{}' produced the wrong widget class", typeId);
    widget->_stableKey = std::move(key);
    widget->_name      = displayName.empty() ? widget->_stableKey : std::move(displayName);
    widget->_bAutoSize = true;
    return widget;
}

template<typename TWidget, typename TDerived>
class TUIWidgetBuilder
{
  public:
    explicit TUIWidgetBuilder(const char* typeId, std::string key, std::string displayName = {})
        : _widget(makeLiveWidget<TWidget>(typeId, std::move(key), std::move(displayName)))
    {
    }

    explicit TUIWidgetBuilder(std::shared_ptr<TWidget> widget)
        : _widget(std::move(widget))
    {
    }

    [[nodiscard]] TWidget& widget() { return *_widget; }
    [[nodiscard]] const TWidget& widget() const { return *_widget; }

    [[nodiscard]] UIElementRef release()
    {
        YA_CORE_ASSERT(_widget, "ui builder already released");
        return std::move(_widget);
    }

    /// Extra strong ref for page handles / tests. Valid until the widget dies;
    /// does not keep the builder from `release()`.
    [[nodiscard]] std::shared_ptr<TWidget> share() const { return _widget; }

    [[nodiscard]] TDerived& setPosition(const glm::vec2& value) &
    {
        _widget->setPosition(value);
        return derived();
    }

    [[nodiscard]] TDerived&& setPosition(const glm::vec2& value) &&
    {
        _widget->setPosition(value);
        return std::move(derived());
    }

    [[nodiscard]] TDerived& setSize(const glm::vec2& value) &
    {
        _widget->_bAutoSize = false;
        _widget->setSize(value);
        return derived();
    }

    [[nodiscard]] TDerived&& setSize(const glm::vec2& value) &&
    {
        _widget->_bAutoSize = false;
        _widget->setSize(value);
        return std::move(derived());
    }

    [[nodiscard]] TDerived& setEnabled(bool value) &
    {
        _widget->setEnabled(value);
        return derived();
    }

    [[nodiscard]] TDerived&& setEnabled(bool value) &&
    {
        _widget->setEnabled(value);
        return std::move(derived());
    }

    [[nodiscard]] TDerived& setVisibility(EWidgetVisibility value) &
    {
        _widget->setVisibility(value);
        return derived();
    }

    [[nodiscard]] TDerived&& setVisibility(EWidgetVisibility value) &&
    {
        _widget->setVisibility(value);
        return std::move(derived());
    }

    [[nodiscard]] TDerived& setTooltip(std::string value) &
    {
        _widget->setTooltip(std::move(value));
        return derived();
    }

    [[nodiscard]] TDerived&& setTooltip(std::string value) &&
    {
        _widget->setTooltip(std::move(value));
        return std::move(derived());
    }

    [[nodiscard]] TDerived& setFocusPolicy(EWidgetFocusPolicy value) &
    {
        _widget->_focusPolicy = value;
        return derived();
    }

    [[nodiscard]] TDerived&& setFocusPolicy(EWidgetFocusPolicy value) &&
    {
        _widget->_focusPolicy = value;
        return std::move(derived());
    }

    [[nodiscard]] TDerived& fillParent() &
    {
        _widget->_anchorMin = {0.0f, 0.0f};
        _widget->_anchorMax = {1.0f, 1.0f};
        return derived();
    }

    [[nodiscard]] TDerived&& fillParent() &&
    {
        _widget->_anchorMin = {0.0f, 0.0f};
        _widget->_anchorMax = {1.0f, 1.0f};
        return std::move(derived());
    }

    [[nodiscard]] TDerived& setAnchors(const glm::vec2& min, const glm::vec2& max) &
    {
        _widget->_anchorMin = min;
        _widget->_anchorMax = max;
        return derived();
    }

    [[nodiscard]] TDerived&& setAnchors(const glm::vec2& min, const glm::vec2& max) &&
    {
        _widget->_anchorMin = min;
        _widget->_anchorMax = max;
        return std::move(derived());
    }

  protected:
    std::shared_ptr<TWidget> _widget;

    [[nodiscard]] TDerived& derived() & { return static_cast<TDerived&>(*this); }
    [[nodiscard]] TDerived&& derived() && { return static_cast<TDerived&&>(*this); }
};

template<typename TWidget, typename TDerived>
class TUIWidgetChildrenBuilder : public TUIWidgetBuilder<TWidget, TDerived>
{
  public:
    using TUIWidgetBuilder<TWidget, TDerived>::TUIWidgetBuilder;

    TDerived& child(UIElementRef node) &
    {
        this->_widget->addDetachedChild(std::move(node));
        return this->derived();
    }

    TDerived&& child(UIElementRef node) &&
    {
        this->_widget->addDetachedChild(std::move(node));
        return std::move(this->derived());
    }

    template<UIWidgetBuilder TChild>
    TDerived& child(TChild&& builder) &
    {
        this->_widget->addDetachedChild(std::forward<TChild>(builder).release());
        return this->derived();
    }

    template<UIWidgetBuilder TChild>
    TDerived&& child(TChild&& builder) &&
    {
        this->_widget->addDetachedChild(std::forward<TChild>(builder).release());
        return std::move(this->derived());
    }

    template<typename... TChildren>
    [[nodiscard]] TDerived& children(TChildren&&... nodes) &
    {
        (child(std::forward<TChildren>(nodes)), ...);
        return this->derived();
    }

    template<typename... TChildren>
    [[nodiscard]] TDerived&& children(TChildren&&... nodes) &&
    {
        (child(std::forward<TChildren>(nodes)), ...);
        return std::move(this->derived());
    }
};

template<UICompoundWidgetType TWidget>
class TUICompoundWidgetBuilder final : public TUIWidgetBuilder<TWidget, TUICompoundWidgetBuilder<TWidget>>
{
  public:
    template<typename... TArgs>
    explicit TUICompoundWidgetBuilder(std::string key, std::string displayName, TArgs&&... args)
        : TUIWidgetBuilder<TWidget, TUICompoundWidgetBuilder<TWidget>>(makeCompoundWidget(std::move(key), std::move(displayName), std::forward<TArgs>(args)...))
    {
    }

  private:
    template<typename... TArgs>
    [[nodiscard]] static std::shared_ptr<TWidget> makeCompoundWidget(std::string key,
                                                                     std::string displayName,
                                                                     TArgs&&... args)
    {
        const std::string resolvedName = displayName.empty() ? key : displayName;
        auto widget = std::make_shared<TWidget>(resolvedName, std::forward<TArgs>(args)...);
        widget->_stableKey = std::move(key);
        widget->_name = resolvedName;
        widget->_bAutoSize = true;
        return widget;
    }
};

class UITextWidgetBuilder final : public TUIWidgetBuilder<UIText, UITextWidgetBuilder>
{
  public:
    explicit UITextWidgetBuilder(std::string key, std::string displayName = {})
        : TUIWidgetBuilder(kTypeIdText, std::move(key), std::move(displayName))
    {
    }

    [[nodiscard]] UITextWidgetBuilder& setText(const std::string& value) &
    {
        _widget->setText(value);
        return *this;
    }

    [[nodiscard]] UITextWidgetBuilder&& setText(const std::string& value) &&
    {
        _widget->setText(value);
        return std::move(*this);
    }

    [[nodiscard]] UITextWidgetBuilder& bindText(std::shared_ptr<Reactive<std::string>> ref) &
    {
        _widget->bindText(std::move(ref));
        return *this;
    }

    [[nodiscard]] UITextWidgetBuilder&& bindText(std::shared_ptr<Reactive<std::string>> ref) &&
    {
        _widget->bindText(std::move(ref));
        return std::move(*this);
    }

    [[nodiscard]] UITextWidgetBuilder& setFontSize(uint32_t value) &
    {
        _widget->setFontSize(value);
        return *this;
    }

    [[nodiscard]] UITextWidgetBuilder&& setFontSize(uint32_t value) &&
    {
        _widget->setFontSize(value);
        return std::move(*this);
    }

    [[nodiscard]] UITextWidgetBuilder& setColor(const glm::vec4& value) &
    {
        _widget->setColor(value);
        return *this;
    }

    [[nodiscard]] UITextWidgetBuilder&& setColor(const glm::vec4& value) &&
    {
        _widget->setColor(value);
        return std::move(*this);
    }

    [[nodiscard]] UITextWidgetBuilder& setHAlign(EWidgetAlignH value) &
    {
        _widget->_hAlign = value;
        return *this;
    }

    [[nodiscard]] UITextWidgetBuilder&& setHAlign(EWidgetAlignH value) &&
    {
        _widget->_hAlign = value;
        return std::move(*this);
    }

    [[nodiscard]] UITextWidgetBuilder& setVAlign(EWidgetAlignV value) &
    {
        _widget->_vAlign = value;
        return *this;
    }

    [[nodiscard]] UITextWidgetBuilder&& setVAlign(EWidgetAlignV value) &&
    {
        _widget->_vAlign = value;
        return std::move(*this);
    }

    [[nodiscard]] UITextWidgetBuilder& setFillBackground(bool value) &
    {
        _widget->_bFillBackground = value;
        return *this;
    }

    [[nodiscard]] UITextWidgetBuilder&& setFillBackground(bool value) &&
    {
        _widget->_bFillBackground = value;
        return std::move(*this);
    }

    [[nodiscard]] UITextWidgetBuilder& setWrap(bool value) &
    {
        _widget->_bWrap = value;
        return *this;
    }

    [[nodiscard]] UITextWidgetBuilder&& setWrap(bool value) &&
    {
        _widget->_bWrap = value;
        return std::move(*this);
    }

    [[nodiscard]] UITextWidgetBuilder& setMaxWrapWidth(float value) &
    {
        _widget->_maxWrapWidth = value;
        return *this;
    }

    [[nodiscard]] UITextWidgetBuilder&& setMaxWrapWidth(float value) &&
    {
        _widget->_maxWrapWidth = value;
        return std::move(*this);
    }
};

class UIButtonWidgetBuilder final : public TUIWidgetChildrenBuilder<UIButton, UIButtonWidgetBuilder>
{
  public:
    explicit UIButtonWidgetBuilder(std::string key, std::string displayName = {})
        : TUIWidgetChildrenBuilder(kTypeIdButton, std::move(key), std::move(displayName))
    {
    }

    [[nodiscard]] UIButtonWidgetBuilder& setOnClick(std::function<void()> value) &
    {
        _widget->_onClick = std::move(value);
        return *this;
    }

    [[nodiscard]] UIButtonWidgetBuilder&& setOnClick(std::function<void()> value) &&
    {
        _widget->_onClick = std::move(value);
        return std::move(*this);
    }

    [[nodiscard]] UIButtonWidgetBuilder& bindEnabled(std::shared_ptr<Reactive<bool>> ref) &
    {
        _widget->bindEnabled(std::move(ref));
        return *this;
    }

    [[nodiscard]] UIButtonWidgetBuilder&& bindEnabled(std::shared_ptr<Reactive<bool>> ref) &&
    {
        _widget->bindEnabled(std::move(ref));
        return std::move(*this);
    }

    [[nodiscard]] UIButtonWidgetBuilder& setContentPadding(glm::vec2 value) &
    {
        _widget->setContentPadding(value);
        return *this;
    }

    [[nodiscard]] UIButtonWidgetBuilder&& setContentPadding(glm::vec2 value) &&
    {
        _widget->setContentPadding(value);
        return std::move(*this);
    }
};

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

    [[nodiscard]] UIPanelWidgetBuilder& setStyleKey(std::string value) &
    {
        _widget->_styleKey = std::move(value);
        return *this;
    }

    [[nodiscard]] UIPanelWidgetBuilder&& setStyleKey(std::string value) &&
    {
        _widget->_styleKey = std::move(value);
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

    /// After `addDetachedChild`, mark the new child's box slot Fill so it
    /// takes leftover main-axis space (e.g. a split under header labels).
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

class UITextFieldWidgetBuilder final : public TUIWidgetBuilder<UITextField, UITextFieldWidgetBuilder>
{
  public:
    explicit UITextFieldWidgetBuilder(std::string key, std::string displayName = {})
        : TUIWidgetBuilder(kTypeIdTextField, std::move(key), std::move(displayName))
    {
    }

    [[nodiscard]] UITextFieldWidgetBuilder& setText(const std::string& value) &
    {
        _widget->setText(value);
        return *this;
    }

    [[nodiscard]] UITextFieldWidgetBuilder&& setText(const std::string& value) &&
    {
        _widget->setText(value);
        return std::move(*this);
    }

    [[nodiscard]] UITextFieldWidgetBuilder& setFontSize(uint32_t value) &
    {
        _widget->setFontSize(value);
        return *this;
    }

    [[nodiscard]] UITextFieldWidgetBuilder&& setFontSize(uint32_t value) &&
    {
        _widget->setFontSize(value);
        return std::move(*this);
    }

    [[nodiscard]] UITextFieldWidgetBuilder& setOnCommit(std::function<void(const std::string&)> value) &
    {
        _widget->_onCommit = std::move(value);
        return *this;
    }

    [[nodiscard]] UITextFieldWidgetBuilder&& setOnCommit(std::function<void(const std::string&)> value) &&
    {
        _widget->_onCommit = std::move(value);
        return std::move(*this);
    }

    [[nodiscard]] UITextFieldWidgetBuilder& setOnTextChanged(std::function<void(const std::string&)> value) &
    {
        _widget->_onTextChanged = std::move(value);
        return *this;
    }

    [[nodiscard]] UITextFieldWidgetBuilder&& setOnTextChanged(std::function<void(const std::string&)> value) &&
    {
        _widget->_onTextChanged = std::move(value);
        return std::move(*this);
    }
};

class UICheckBoxWidgetBuilder final : public TUIWidgetChildrenBuilder<UICheckBox, UICheckBoxWidgetBuilder>
{
  public:
    explicit UICheckBoxWidgetBuilder(std::string key, std::string displayName = {})
        : TUIWidgetChildrenBuilder(kTypeIdCheckBox, std::move(key), std::move(displayName))
    {
    }

    [[nodiscard]] UICheckBoxWidgetBuilder& setChecked(bool value) &
    {
        _widget->_bChecked = value;
        return *this;
    }

    [[nodiscard]] UICheckBoxWidgetBuilder&& setChecked(bool value) &&
    {
        _widget->_bChecked = value;
        return std::move(*this);
    }

    [[nodiscard]] UICheckBoxWidgetBuilder& setText(const std::string& value) &
    {
        child(UITextWidgetBuilder{_widget->_stableKey + "__label"}.setText(value));
        return *this;
    }

    [[nodiscard]] UICheckBoxWidgetBuilder&& setText(const std::string& value) &&
    {
        child(UITextWidgetBuilder{_widget->_stableKey + "__label"}.setText(value));
        return std::move(*this);
    }

    [[nodiscard]] UICheckBoxWidgetBuilder& setOnChanged(std::function<void(bool)> value) &
    {
        _widget->_onChanged = std::move(value);
        return *this;
    }

    [[nodiscard]] UICheckBoxWidgetBuilder&& setOnChanged(std::function<void(bool)> value) &&
    {
        _widget->_onChanged = std::move(value);
        return std::move(*this);
    }
};

class UISliderWidgetBuilder final : public TUIWidgetBuilder<UISlider, UISliderWidgetBuilder>
{
  public:
    explicit UISliderWidgetBuilder(std::string key, std::string displayName = {})
        : TUIWidgetBuilder(kTypeIdSlider, std::move(key), std::move(displayName))
    {
    }

    [[nodiscard]] UISliderWidgetBuilder& setValue(float value) &
    {
        _widget->setValue(value);
        return *this;
    }

    [[nodiscard]] UISliderWidgetBuilder&& setValue(float value) &&
    {
        _widget->setValue(value);
        return std::move(*this);
    }

    [[nodiscard]] UISliderWidgetBuilder& setOnValueChanged(std::function<void(float)> value) &
    {
        _widget->_onValueChanged = std::move(value);
        return *this;
    }

    [[nodiscard]] UISliderWidgetBuilder&& setOnValueChanged(std::function<void(float)> value) &&
    {
        _widget->_onValueChanged = std::move(value);
        return std::move(*this);
    }
};

class UIComboBoxWidgetBuilder final : public TUIWidgetBuilder<UIComboBox, UIComboBoxWidgetBuilder>
{
  public:
    explicit UIComboBoxWidgetBuilder(std::string key, std::string displayName = {})
        : TUIWidgetBuilder(kTypeIdComboBox, std::move(key), std::move(displayName))
    {
    }

    [[nodiscard]] UIComboBoxWidgetBuilder& setItems(std::vector<std::string> value) &
    {
        _widget->_items = std::move(value);
        return *this;
    }

    [[nodiscard]] UIComboBoxWidgetBuilder&& setItems(std::vector<std::string> value) &&
    {
        _widget->_items = std::move(value);
        return std::move(*this);
    }

    [[nodiscard]] UIComboBoxWidgetBuilder& setSelectedIndex(int value) &
    {
        _widget->_selectedIndex = value;
        return *this;
    }

    [[nodiscard]] UIComboBoxWidgetBuilder&& setSelectedIndex(int value) &&
    {
        _widget->_selectedIndex = value;
        return std::move(*this);
    }

    [[nodiscard]] UIComboBoxWidgetBuilder& setOnSelectionChanged(std::function<void(int)> value) &
    {
        _widget->_onSelectionChanged = std::move(value);
        return *this;
    }

    [[nodiscard]] UIComboBoxWidgetBuilder&& setOnSelectionChanged(std::function<void(int)> value) &&
    {
        _widget->_onSelectionChanged = std::move(value);
        return std::move(*this);
    }
};

class UIImageWidgetBuilder final : public TUIWidgetBuilder<UIImage, UIImageWidgetBuilder>
{
  public:
    explicit UIImageWidgetBuilder(std::string key, std::string displayName = {})
        : TUIWidgetBuilder(kTypeIdImage, std::move(key), std::move(displayName))
    {
    }

    [[nodiscard]] UIImageWidgetBuilder& setAssetPath(std::string value) &
    {
        _widget->_assetPath = std::move(value);
        return *this;
    }

    [[nodiscard]] UIImageWidgetBuilder&& setAssetPath(std::string value) &&
    {
        _widget->_assetPath = std::move(value);
        return std::move(*this);
    }

    [[nodiscard]] UIImageWidgetBuilder& setTexture(std::shared_ptr<Texture> value) &
    {
        _widget->setTexture(std::move(value));
        return *this;
    }

    [[nodiscard]] UIImageWidgetBuilder&& setTexture(std::shared_ptr<Texture> value) &&
    {
        _widget->setTexture(std::move(value));
        return std::move(*this);
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

template<UICompoundWidgetType TWidget, typename... TArgs>
[[nodiscard]] inline TUICompoundWidgetBuilder<TWidget> compound(std::string key,
                                                                std::string displayName = {},
                                                                TArgs&&... args)
{
    return TUICompoundWidgetBuilder<TWidget>{std::move(key),
                                             std::move(displayName),
                                             std::forward<TArgs>(args)...};
}

/// Attach a constructed builder to `parent`. Assemble the builder first,
/// then call this as its own statement — do not wrap the DSL in `build(...)`.
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
