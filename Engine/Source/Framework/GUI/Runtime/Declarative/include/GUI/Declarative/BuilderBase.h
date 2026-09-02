#pragma once

#include "Core/Log.h"

#include "GUI/Declarative/LayoutSpec.h"
#include "GUI/Layout/UILayout.h"
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
concept UISlotBuilder = requires(const std::remove_reference_t<T>& builder) {
    builder.args();
} && (std::same_as<std::remove_cvref_t<decltype(std::declval<const std::remove_reference_t<T>&>().args())>, FCanvasSlotArgs> ||
      std::same_as<std::remove_cvref_t<decltype(std::declval<const std::remove_reference_t<T>&>().args())>, FBoxSlotArgs> ||
      std::same_as<std::remove_cvref_t<decltype(std::declval<const std::remove_reference_t<T>&>().args())>, FOverlaySlotArgs>);

template<typename T>
concept UICompoundWidgetType = std::derived_from<T, UICompoundWidget>;

/// Forward declaration: a layout spec bound to a child (defined below).
/// The capability set is part of the type so hosts can reject unsupported
/// intent at compile time.
template<EUILayoutCap Caps, typename TChild>
struct TUILayoutAttachment;

template<typename THost, EUILayoutCap Caps>
concept LayoutAttachmentAcceptedBy = LayoutCapsCompatible<Caps, allowedLayoutCaps<THost>()>;

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

    template<UIWidgetBuilder TChild, UISlotBuilder TSlotBuilder>
    TDerived& child(TChild&& builder, TSlotBuilder&& slotBuilder) &
    {
        applySlotBuilder(std::forward<TChild>(builder).release(), std::forward<TSlotBuilder>(slotBuilder));
        return this->derived();
    }

    template<UIWidgetBuilder TChild, UISlotBuilder TSlotBuilder>
    TDerived&& child(TChild&& builder, TSlotBuilder&& slotBuilder) &&
    {
        applySlotBuilder(std::forward<TChild>(builder).release(), std::forward<TSlotBuilder>(slotBuilder));
        return std::move(this->derived());
    }

    template<UISlotBuilder TSlotBuilder>
    TDerived& child(UIElementRef node, TSlotBuilder&& slotBuilder) &
    {
        applySlotBuilder(std::move(node), std::forward<TSlotBuilder>(slotBuilder));
        return this->derived();
    }

    template<UISlotBuilder TSlotBuilder>
    TDerived&& child(UIElementRef node, TSlotBuilder&& slotBuilder) &&
    {
        applySlotBuilder(std::move(node), std::forward<TSlotBuilder>(slotBuilder));
        return std::move(this->derived());
    }

    template<EUILayoutCap Caps, typename TChild>
        requires LayoutAttachmentAcceptedBy<TDerived, Caps>
    TDerived& child(TUILayoutAttachment<Caps, TChild> attachment) &
    {
        applyLayout(takeElementRef(std::move(attachment.child)), attachment.spec);
        return this->derived();
    }

    template<EUILayoutCap Caps, typename TChild>
        requires LayoutAttachmentAcceptedBy<TDerived, Caps>
    TDerived&& child(TUILayoutAttachment<Caps, TChild> attachment) &&
    {
        applyLayout(takeElementRef(std::move(attachment.child)), attachment.spec);
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
        using TArgs = std::remove_cvref_t<decltype(slotBuilder.args())>;
        if constexpr (std::same_as<TArgs, FCanvasSlotArgs>) {
            applyCanvasSlot(std::move(node), slotBuilder.args());
        }
        else if constexpr (std::same_as<TArgs, FBoxSlotArgs>) {
            attachChild(std::move(node), [&slotBuilder](UIElement&, UISlot& childSlot) {
                if (auto* typedSlot = childSlot.as<UIBoxSlot>()) {
                    typedSlot->apply(slotBuilder.args());
                }
            });
        }
        else {
            applySingleChildSlot(std::move(node), slotBuilder.args());
        }
    }

    void attachChild(UIElementRef node)
    {
        this->_widget->addDetachedChild(std::move(node), [](UIElement&, UISlot&) {});
    }

    template<typename TSlotInit>
    void attachChild(UIElementRef node, TSlotInit&& init)
    {
        this->_widget->addDetachedChild(std::move(node), std::forward<TSlotInit>(init));
    }

    /// Attach a child and apply its single-child slot intent. Parents that own
    /// both axes (button / selectable row / scroll / size box / split pane ...)
    /// must route their child(node, slot) overloads through here so intent lands
    /// on the edge instead of on the child's ignored anchors.
    void applySingleChildSlot(UIElementRef node, const FOverlaySlotArgs& slot)
    {
        attachChild(std::move(node), [&slot](UIElement&, UISlot& childSlot) {
            if (auto* typedSlot = childSlot.as<UIOverlaySlot>()) {
                typedSlot->apply(slot);
            }
        });
    }

    /// Attach a child and apply its canvas slot intent. The anchor geometry
    /// lives on this edge, never on the child's ignored anchors.
    void applyCanvasSlot(UIElementRef node, const FCanvasSlotArgs& slot)
    {
        attachChild(std::move(node), [&slot](UIElement&, UISlot& childSlot) {
            if (auto* typedSlot = childSlot.as<UICanvasSlot>()) {
                typedSlot->apply(slot);
            }
        });
    }

    /// Attach a child with a unified layout spec. The host consumes the
    /// capabilities it implements and reports the rest, so intent is never
    /// silently dropped.
    void applyLayout(UIElementRef node, const FUILayoutSpec& spec)
    {
        attachChild(std::move(node), [&spec](UIElement& child, UISlot& slot) {
            applyLayoutSpecToSlot(slot, child, spec);
        });
    }

  public:
    /// Unified attach: `parent[ui::layout().fill() >> widget]`.
    ///
    /// The capability set is checked against the host's declared set at compile
    /// time, so an intent the host cannot honour never compiles instead of being
    /// silently dropped. Hosts declare their set via `kAllowedLayoutCaps`;
    /// builders without one stay permissive until migrated.
    template<EUILayoutCap Caps, typename TChild>
        requires LayoutAttachmentAcceptedBy<TDerived, Caps>
    TDerived& operator[](TUILayoutAttachment<Caps, TChild> attachment) &
    {
        applyLayout(takeElementRef(std::move(attachment.child)), attachment.spec);
        return static_cast<TDerived&>(*this);
    }

    template<EUILayoutCap Caps, typename TChild>
        requires LayoutAttachmentAcceptedBy<TDerived, Caps>
    TDerived&& operator[](TUILayoutAttachment<Caps, TChild> attachment) &&
    {
        applyLayout(takeElementRef(std::move(attachment.child)), attachment.spec);
        return std::move(static_cast<TDerived&>(*this));
    }

    /// Apply a spec to an already-attached child (the host resolution step).
    void applyLayoutSpec(UIElement& child, const FUILayoutSpec& spec)
    {
        this->_widget->initializeChildSlot(child, [&spec](UIElement& live, UISlot& slot) {
            applyLayoutSpecToSlot(slot, live, spec);
        });
    }
};

/// A layout spec bound to a child: the result of `ui::layout().fill() >> widget`.
template<EUILayoutCap Caps, typename TChild>
struct TUILayoutAttachment
{
    FUILayoutSpec spec{};
    TChild        child{};
};

/// `layoutSpec >> widget` binds intent to a child for `parent[...]`.
template<EUILayoutCap Caps, typename TChild>
[[nodiscard]] inline TUILayoutAttachment<Caps, TChild> operator>>(const FUILayoutSpec& spec, TChild&& child)
{
    return TUILayoutAttachment<Caps, TChild>{spec, std::forward<TChild>(child)};
}

// NOTE: the `FUILayoutSpecBuilder >> widget` overload lives in SlotBuilders.h,
// next to the builder type it binds.

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
