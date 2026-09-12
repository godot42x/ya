#pragma once

#include "GUI/Declarative/BuilderBase.h"

#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/CheckBox.h"
#include "GUI/Widgets/Controls/ComboBox.h"
#include "GUI/Widgets/Controls/Image.h"
#include "GUI/Widgets/Controls/SelectableRow.h"
#include "GUI/Widgets/Controls/Slider.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TextField.h"

namespace ya::ui
{

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
    using SlotArgs = FContentSlotArgs;

    explicit UIButtonWidgetBuilder(std::string key, std::string displayName = {})
        : TUIWidgetChildrenBuilder(kTypeIdButton, std::move(key), std::move(displayName))
    {
    }

    // The button owns its content box, so the label's intent is how it sits in
    // that box: fill (default) or align at desired size. The using keeps the
    // plain child(node) overloads visible.
    using TUIWidgetChildrenBuilder::child;

    UIButtonWidgetBuilder& child(UIElementRef node, const FContentSlotArgs& slot) &
    {
        this->applySlotArgs(std::move(node), slot);
        return *this;
    }
    UIButtonWidgetBuilder&& child(UIElementRef node, const FContentSlotArgs& slot) &&
    {
        this->applySlotArgs(std::move(node), slot);
        return std::move(*this);
    }

    template<UIWidgetBuilder TChild>
    UIButtonWidgetBuilder& child(TChild&& builder, const FContentSlotArgs& slot) &
    {
        this->applySlotArgs(std::forward<TChild>(builder).release(), slot);
        return *this;
    }
    template<UIWidgetBuilder TChild>
    UIButtonWidgetBuilder&& child(TChild&& builder, const FContentSlotArgs& slot) &&
    {
        this->applySlotArgs(std::forward<TChild>(builder).release(), slot);
        return std::move(*this);
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
    using SlotArgs = FContentSlotArgs;

    explicit UICheckBoxWidgetBuilder(std::string key, std::string displayName = {})
        : TUIWidgetChildrenBuilder(kTypeIdCheckBox, std::move(key), std::move(displayName))
    {
    }

    using TUIWidgetChildrenBuilder::child;

    UICheckBoxWidgetBuilder& child(UIElementRef node, const FContentSlotArgs& slot) &
    {
        this->applySlotArgs(std::move(node), slot);
        return *this;
    }
    UICheckBoxWidgetBuilder&& child(UIElementRef node, const FContentSlotArgs& slot) &&
    {
        this->applySlotArgs(std::move(node), slot);
        return std::move(*this);
    }

    template<UIWidgetBuilder TChild>
    UICheckBoxWidgetBuilder& child(TChild&& builder, const FContentSlotArgs& slot) &
    {
        this->applySlotArgs(std::forward<TChild>(builder).release(), slot);
        return *this;
    }
    template<UIWidgetBuilder TChild>
    UICheckBoxWidgetBuilder&& child(TChild&& builder, const FContentSlotArgs& slot) &&
    {
        this->applySlotArgs(std::forward<TChild>(builder).release(), slot);
        return std::move(*this);
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

/// List row: a stable item id + select/activate callbacks, plus one content
/// child (usually a UIText label). The row paints its own selection state, so
/// the DSL exposes the callbacks rather than a text property. The row owns
/// a single-child content box; indent/fill live on that edge.
class UISelectableRowWidgetBuilder final : public TUIWidgetChildrenBuilder<UISelectableRow, UISelectableRowWidgetBuilder>
{
  public:
    using SlotArgs = FContentSlotArgs;

    explicit UISelectableRowWidgetBuilder(std::string key, std::string displayName = {})
        : TUIWidgetChildrenBuilder(kTypeIdSelectableRow, std::move(key), std::move(displayName))
    {
    }

    using TUIWidgetChildrenBuilder::child;

    UISelectableRowWidgetBuilder& child(UIElementRef node, const FContentSlotArgs& slot) &
    {
        this->applySlotArgs(std::move(node), slot);
        return *this;
    }
    UISelectableRowWidgetBuilder&& child(UIElementRef node, const FContentSlotArgs& slot) &&
    {
        this->applySlotArgs(std::move(node), slot);
        return std::move(*this);
    }

    template<UIWidgetBuilder TChild>
    UISelectableRowWidgetBuilder& child(TChild&& builder, const FContentSlotArgs& slot) &
    {
        this->applySlotArgs(std::forward<TChild>(builder).release(), slot);
        return *this;
    }
    template<UIWidgetBuilder TChild>
    UISelectableRowWidgetBuilder&& child(TChild&& builder, const FContentSlotArgs& slot) &&
    {
        this->applySlotArgs(std::forward<TChild>(builder).release(), slot);
        return std::move(*this);
    }

    [[nodiscard]] UISelectableRowWidgetBuilder& setItemId(std::string value) &
    {
        _widget->_itemId = std::move(value);
        return *this;
    }

    [[nodiscard]] UISelectableRowWidgetBuilder&& setItemId(std::string value) &&
    {
        _widget->_itemId = std::move(value);
        return std::move(*this);
    }

    [[nodiscard]] UISelectableRowWidgetBuilder& setSelected(bool value) &
    {
        _widget->setSelected(value);
        return *this;
    }

    [[nodiscard]] UISelectableRowWidgetBuilder&& setSelected(bool value) &&
    {
        _widget->setSelected(value);
        return std::move(*this);
    }

    [[nodiscard]] UISelectableRowWidgetBuilder& setOnSelect(std::function<void(const std::string&)> value) &
    {
        _widget->_onSelect = std::move(value);
        return *this;
    }

    [[nodiscard]] UISelectableRowWidgetBuilder&& setOnSelect(std::function<void(const std::string&)> value) &&
    {
        _widget->_onSelect = std::move(value);
        return std::move(*this);
    }

    [[nodiscard]] UISelectableRowWidgetBuilder& setOnActivate(std::function<void(const std::string&)> value) &
    {
        _widget->_onActivate = std::move(value);
        return *this;
    }

    [[nodiscard]] UISelectableRowWidgetBuilder&& setOnActivate(std::function<void(const std::string&)> value) &&
    {
        _widget->_onActivate = std::move(value);
        return std::move(*this);
    }

    [[nodiscard]] UISelectableRowWidgetBuilder& setContentPadding(FMargin value) &
    {
        _widget->setContentPadding(value);
        return *this;
    }

    [[nodiscard]] UISelectableRowWidgetBuilder&& setContentPadding(FMargin value) &&
    {
        _widget->setContentPadding(value);
        return std::move(*this);
    }

    [[nodiscard]] UISelectableRowWidgetBuilder& setContentPadding(glm::vec2 value) &
    {
        _widget->setContentPadding(value);
        return *this;
    }

    [[nodiscard]] UISelectableRowWidgetBuilder&& setContentPadding(glm::vec2 value) &&
    {
        _widget->setContentPadding(value);
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
        _widget->setAssetPath(std::move(value));
        return *this;
    }

    [[nodiscard]] UIImageWidgetBuilder&& setAssetPath(std::string value) &&
    {
        _widget->setAssetPath(std::move(value));
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

    [[nodiscard]] UIImageWidgetBuilder& setScaleMode(EImageScaleMode value) &
    {
        _widget->setScaleMode(value);
        return *this;
    }

    [[nodiscard]] UIImageWidgetBuilder&& setScaleMode(EImageScaleMode value) &&
    {
        _widget->setScaleMode(value);
        return std::move(*this);
    }
};

} // namespace ya::ui
