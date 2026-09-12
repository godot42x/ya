#pragma once

#include "Core/Log.h"

#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/CompoundWidget.h"
#include "GUI/Widgets/UITypeRegistry.h"
#include "GUI/Widgets/WidgetTree.h"

#include <concepts>
#include <algorithm>
#include <memory>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace ya::ui
{

template<typename T>
concept UIWidgetBuilder = requires(T&& builder) {
    { std::forward<T>(builder).release() } -> std::convertible_to<UIElementRef>;
};

/// A fragment is a purely authoring-time group. It never becomes a runtime
/// widget, so it cannot introduce a second tree or reconciliation layer.
template<typename... TItems>
class TUIChildrenFragment
{
  public:
    template<typename... TArgs>
    explicit TUIChildrenFragment(TArgs&&... items) : _items(std::forward<TArgs>(items)...)
    {
    }

    template<typename TParent>
    void appendTo(TParent& parent) &&
    {
        std::apply([&parent](auto&&... items) { (parent.child(std::forward<decltype(items)>(items)), ...); },
                   std::move(_items));
    }

  private:
    std::tuple<TItems...> _items;
};

template<typename T>
concept UIChildrenFragment = requires(T&& fragment, T& parent) {
    std::forward<T>(fragment).appendTo(parent);
};

template<typename TItem>
class TUIConditionalChild
{
  public:
    template<typename TArg>
    TUIConditionalChild(bool condition, TArg&& item)
        : _condition(condition), _item(std::forward<TItem>(item))
    {
    }

    template<typename TParent>
    void appendTo(TParent& parent) &&
    {
        if (_condition) {
            parent.child(std::move(*_item));
        }
    }

  private:
    bool _condition = false;
    std::optional<TItem> _item;
};

template<typename TThen, typename TElse>
class TUIIfElseChild
{
  public:
    template<typename TThenArg, typename TElseArg>
    TUIIfElseChild(bool condition, TThenArg&& thenItem, TElseArg&& elseItem)
        : _condition(condition), _thenItem(std::forward<TThen>(thenItem)), _elseItem(std::forward<TElse>(elseItem))
    {
    }

    template<typename TParent>
    void appendTo(TParent& parent) &&
    {
        if (_condition) {
            parent.child(std::move(*_thenItem));
        }
        else {
            parent.child(std::move(*_elseItem));
        }
    }

  private:
    bool _condition = false;
    std::optional<TThen> _thenItem;
    std::optional<TElse> _elseItem;
};

template<typename T>
concept UISlotBuilder = requires(const std::remove_reference_t<T>& builder) {
    builder.args();
    typename std::remove_cvref_t<decltype(std::declval<const std::remove_reference_t<T>&>().args())>::SlotType;
};

template<typename T>
concept UICompoundWidgetType = std::derived_from<T, UICompoundWidget>;

template<typename THost, typename TSlotBuilder>
concept SlotBuilderAcceptedBy = UISlotBuilder<TSlotBuilder> && requires {
    typename THost::SlotArgs;
} && std::same_as<
        std::remove_cvref_t<decltype(std::declval<const std::remove_reference_t<TSlotBuilder>&>().args())>,
        typename THost::SlotArgs>;

/// Take a UIElementRef from either a builder (via release()) or an already-built
/// shared_ptr, so `spec >> widget` accepts both forms.
template<typename TChild>
[[nodiscard]] inline UIElementRef takeElementRef(TChild&& child)
{
    if constexpr (requires { std::forward<TChild>(child).release(); }) {
        return std::forward<TChild>(child).release();
    }
    else {
        return std::forward<TChild>(child);
    }
}

template<typename TWidget>
[[nodiscard]] std::shared_ptr<TWidget> makeLiveWidget(const char* typeId,
                                                      std::string key,
                                                      std::string displayName)
{
    UIElementRef raw = UITypeRegistry::instance().createInstance(typeId);
    auto widget = std::dynamic_pointer_cast<TWidget>(raw);
    YA_CORE_ASSERT(widget, "ui::makeLiveWidget: registry type '{}' produced the wrong widget class", typeId);
    widget->_stableKey = std::move(key);
    // Anonymous DSL nodes intentionally have no stable identity.  Registry
    // factories already seed a useful type label (Text, Panel, ...), so keep
    // that label for diagnostics unless the author opts into a key or a
    // display name.  A display name is presentation/debug text; it never
    // implicitly becomes a stable key.
    if (!displayName.empty()) {
        widget->_name = std::move(displayName);
    }
    else if (!widget->_stableKey.empty()) {
        widget->_name = widget->_stableKey;
    }
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
    [[nodiscard]] UIElementRef take()
    {
        return release();
    }
    template<typename T>
    [[nodiscard]] std::shared_ptr<T> takeAs()
    {
        auto widget = std::dynamic_pointer_cast<T>(release());
        YA_CORE_ASSERT(widget, "ui builder takeAs: produced the wrong widget class");
        return widget;
    }

    [[nodiscard]] std::shared_ptr<TWidget> share() const { return _widget; }

    [[nodiscard]] TDerived& key(std::string value) &
    {
        _widget->_stableKey = std::move(value);
        if (!_widget->_stableKey.empty()) {
            _widget->_name = _widget->_stableKey;
        }
        return derived();
    }

    [[nodiscard]] TDerived&& key(std::string value) &&
    {
        _widget->_stableKey = std::move(value);
        if (!_widget->_stableKey.empty()) {
            _widget->_name = _widget->_stableKey;
        }
        return std::move(derived());
    }

    [[nodiscard]] TDerived& displayName(std::string value) &
    {
        _widget->_name = std::move(value);
        return derived();
    }

    [[nodiscard]] TDerived&& displayName(std::string value) &&
    {
        _widget->_name = std::move(value);
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

    [[nodiscard]] TDerived& setStyleKey(std::string value) &
    {
        _widget->setStyleKey(std::move(value));
        return derived();
    }

    [[nodiscard]] TDerived&& setStyleKey(std::string value) &&
    {
        _widget->setStyleKey(std::move(value));
        return std::move(derived());
    }

    template <typename TStyle>
    [[nodiscard]] TDerived& setStyle(TStyle style) &
        requires requires(TWidget& w, TStyle&& s) { w.setStyle(std::forward<TStyle>(s)); }
    {
        _widget->setStyle(std::move(style));
        return derived();
    }

    template <typename TStyle>
    [[nodiscard]] TDerived&& setStyle(TStyle style) &&
        requires requires(TWidget& w, TStyle&& s) { w.setStyle(std::forward<TStyle>(s)); }
    {
        _widget->setStyle(std::move(style));
        return std::move(derived());
    }

    template <typename TValue>
    [[nodiscard]] TDerived& setStyleField(std::string name, TValue value) &
        requires requires(TWidget& w, std::string n, TValue&& v) {
            w.setStyleField(std::move(n), std::forward<TValue>(v));
        }
    {
        _widget->setStyleField(std::move(name), std::move(value));
        return derived();
    }

    template <typename TValue>
    [[nodiscard]] TDerived&& setStyleField(std::string name, TValue value) &&
        requires requires(TWidget& w, std::string n, TValue&& v) {
            w.setStyleField(std::move(n), std::forward<TValue>(v));
        }
    {
        _widget->setStyleField(std::move(name), std::move(value));
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
        attachChild(std::move(node));
        return this->derived();
    }

    TDerived&& child(UIElementRef node) &&
    {
        attachChild(std::move(node));
        return std::move(this->derived());
    }

    template<UIChildrenFragment TFragment>
    TDerived& child(TFragment&& fragment) &
    {
        std::forward<TFragment>(fragment).appendTo(*this);
        return this->derived();
    }

    template<UIChildrenFragment TFragment>
    TDerived&& child(TFragment&& fragment) &&
    {
        std::forward<TFragment>(fragment).appendTo(*this);
        return std::move(this->derived());
    }

    template<UIWidgetBuilder TChild>
    TDerived& child(TChild&& builder) &
    {
        attachChild(std::forward<TChild>(builder).release());
        return this->derived();
    }

    template<UIWidgetBuilder TChild>
    TDerived&& child(TChild&& builder) &&
    {
        attachChild(std::forward<TChild>(builder).release());
        return std::move(this->derived());
    }

    template<UIWidgetBuilder TChild, typename TSlotBuilder>
        requires SlotBuilderAcceptedBy<TDerived, TSlotBuilder>
    TDerived& child(TChild&& builder, TSlotBuilder&& slotBuilder) &
    {
        applySlotBuilder(std::forward<TChild>(builder).release(), std::forward<TSlotBuilder>(slotBuilder));
        return this->derived();
    }

    template<UIWidgetBuilder TChild, typename TSlotBuilder>
        requires SlotBuilderAcceptedBy<TDerived, TSlotBuilder>
    TDerived&& child(TChild&& builder, TSlotBuilder&& slotBuilder) &&
    {
        applySlotBuilder(std::forward<TChild>(builder).release(), std::forward<TSlotBuilder>(slotBuilder));
        return std::move(this->derived());
    }

    template<typename TSlotBuilder>
        requires SlotBuilderAcceptedBy<TDerived, TSlotBuilder>
    TDerived& child(UIElementRef node, TSlotBuilder&& slotBuilder) &
    {
        applySlotBuilder(std::move(node), std::forward<TSlotBuilder>(slotBuilder));
        return this->derived();
    }

    template<typename TSlotBuilder>
        requires SlotBuilderAcceptedBy<TDerived, TSlotBuilder>
    TDerived&& child(UIElementRef node, TSlotBuilder&& slotBuilder) &&
    {
        applySlotBuilder(std::move(node), std::forward<TSlotBuilder>(slotBuilder));
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

  protected:
    template<UISlotBuilder TSlotBuilder>
    void applySlotBuilder(UIElementRef node, TSlotBuilder&& slotBuilder)
    {
        applySlotArgs(std::move(node), slotBuilder.args());
    }

    template<typename TArgs>
    void applySlotArgs(UIElementRef node, const TArgs& args)
    {
        attachChild(std::move(node), [&args](UIElement&, UISlot& childSlot) {
            childSlot.applyArgs(args);
        });
    }

    void attachChild(UIElementRef node)
    {
        if (!acceptChildKey(node)) {
            return;
        }
        this->_widget->addDetachedChild(std::move(node), [](UIElement&, UISlot&) {});
    }

    template<typename TSlotInit>
    void attachChild(UIElementRef node, TSlotInit&& init)
    {
        if (!acceptChildKey(node)) {
            return;
        }
        this->_widget->addDetachedChild(std::move(node), std::forward<TSlotInit>(init));
    }

    bool acceptChildKey(const UIElementRef& node) const
    {
        if (!node || node->_stableKey.empty()) {
            return true;
        }
        const auto duplicate = std::find_if(this->_widget->getChildren().begin(),
                                             this->_widget->getChildren().end(),
                                             [&node](const UIElementRef& child) {
                                                 return child && child->_stableKey == node->_stableKey;
                                             });
        if (duplicate != this->_widget->getChildren().end()) {
            YA_CORE_ERROR("ui builder '{}': duplicate child key '{}' rejected", this->_widget->_name, node->_stableKey);
            return false;
        }
        return true;
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
        return widget;
    }
};

} // namespace ya::ui
