#pragma once

#include "Core/Log.h"

#include "GUI/Widgets/CompoundWidget.h"
#include "GUI/Widgets/UITypeRegistry.h"
#include "GUI/Widgets/WidgetTree.h"

#include <concepts>
#include <memory>
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

} // namespace ya::ui
