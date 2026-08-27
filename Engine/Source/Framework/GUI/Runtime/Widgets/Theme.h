#pragma once

// ============================================================================
// Theme - the "content" side of the style system (style-system Phase 2).
//
// A UITheme is a named collection of typed styles (backed by a generic
// UIStyleSet). App/game builds one UITheme per look (dark, light, game HUD)
// and mounts it on a WidgetTree; widgets resolve typed styles by key through
// resolveThemeStyle. Mechanism (resolve chain, invalidation) lives in the
// framework; the theme VALUES (tokens -> typed styles) are the app's content.
// ============================================================================

#include "GUI/Widgets/Style.h"
#include "GUI/Widgets/WidgetTree.h"

#include "Core/Reflection/ReflectionSerializer.h"

#include <optional>
#include <utility>

namespace ya
{

/// Named collection of typed styles. Applies per-tree via WidgetTree::setTheme.
struct YA_GUI_API UITheme
{
public:
    /// Define (or replace) a named typed style; re-defining the same key/type
    /// mutates the same Reactive handle (G4 set semantics).
    template <typename TStyle>
    std::shared_ptr<Reactive<TStyle>> define(std::string key, TStyle style)
    {
        return _styles.define(std::move(key), std::move(style));
    }

    /// Find a named typed style; null when undefined.
    template <typename TStyle>
    [[nodiscard]] std::shared_ptr<Reactive<TStyle>> find(const std::string& key) const
    {
        return _styles.find<TStyle>(key);
    }

private:
    UIStyleSet _styles;
};

/// Resolve a typed style for `widget` from its tree's theme. Registers the
/// widget as a dependent of BOTH edges:
///   - the theme-generation token (a theme SWITCH repaints it), and
///   - the specific style Reactive (an edit to THAT style repaints it).
///
/// Returns null when no theme is mounted or the key is absent — the caller
/// falls back to a default-constructed TStyle (the framework fallback).
///
/// Must be called inside paintSelf (so the widget is the current paint
/// widget); layout-affine members (padding/fontSize/minSize/width) pass
/// EDirtyLevel::Layout, color/brush members pass Paint.
template <typename TStyle>
const TStyle* resolveThemeStyle(const UIElement&        widget,
                                const std::string&     key,
                                ReactiveBase::EDirtyLevel level = ReactiveBase::EDirtyLevel::Paint)
{
    WidgetTree* tree = widget.getTree();
    if (!tree || !tree->getTheme()) {
        return nullptr;
    }
    // Unconditional generation edge: a theme switch must repaint this widget
    // even when the resolved style value itself never changed.
    tree->getThemeGeneration()->get(level);
    if (auto style = tree->getTheme()->find<TStyle>(key)) {
        return &style->get(level);
    }
    return nullptr;
}

/// Full resolve chain: instance authored TStyle > style key in
/// the tree theme > default-constructed TStyle. Authored wins without
/// registering a theme-generation edge, so a theme switch does not clobber
/// an instance override. `key` defaults to `widget._styleKey`.
/// Text/Panel `setColor` writes this authored slot (paint-only).
template <typename TStyle>
TStyle resolveWidgetStyle(const UIElement&                 widget,
                          const std::string&               key,
                          const std::optional<TStyle>&     authored,
                          ReactiveBase::EDirtyLevel        level = ReactiveBase::EDirtyLevel::Paint)
{
    if (authored.has_value()) {
        return *authored;
    }
    if (!key.empty()) {
        if (const TStyle* themed = resolveThemeStyle<TStyle>(widget, key, level)) {
            return *themed;
        }
    }
    return TStyle{};
}

template <typename TStyle>
TStyle resolveWidgetStyle(const UIElement&             widget,
                          const std::optional<TStyle>& authored,
                          ReactiveBase::EDirtyLevel    level = ReactiveBase::EDirtyLevel::Paint)
{
    return resolveWidgetStyle<TStyle>(widget, widget._styleKey, authored, level);
}

/// Per-widget authored typed style. High-frequency DSL / MVC / designer
/// path: `setStyle(TStyle)` overrides the theme catalog for this instance.
/// Theme switch is the low-frequency path and only applies when this slot
/// is empty.
template <typename TWidget, typename TStyle>
struct UIStyledWidget
{
    std::optional<TStyle> _authoredStyle;

    void setStyle(TStyle style, EUIPropertyImpact impact = EUIPropertyImpact::Layout)
    {
        auto& self = static_cast<TWidget&>(*this);
        if (_authoredStyle.has_value() && *_authoredStyle == style) {
            return;
        }
        _authoredStyle = std::move(style);
        self.invalidateProperty(impact);
    }

    void clearAuthoredStyle()
    {
        auto& self = static_cast<TWidget&>(*this);
        if (!_authoredStyle.has_value()) {
            return;
        }
        _authoredStyle.reset();
        self.invalidateProperty(EUIPropertyImpact::Layout);
    }

    [[nodiscard]] bool hasAuthoredStyle() const { return _authoredStyle.has_value(); }
};

/// Persist `_authoredStyle` through UIElement's virtual serialize hook.
/// Mixin fields cannot be YA_REFLECT_FIELD'd from a UIElement* (MI offset).
#define YA_GUI_AUTHORED_STYLE_IO(TStyle)                                                                          \
    [[nodiscard]] nlohmann::json serializeAuthoredStyle() const override                                          \
    {                                                                                                             \
        ensureGuiStyleReflection();                                                                               \
        if (!_authoredStyle.has_value()) {                                                                        \
            return nullptr;                                                                                       \
        }                                                                                                         \
        return ReflectionSerializer::serializeByRuntimeReflection(*_authoredStyle);                               \
    }                                                                                                             \
    void deserializeAuthoredStyle(const nlohmann::json& j) override                                               \
    {                                                                                                             \
        ensureGuiStyleReflection();                                                                               \
        if (j.is_null() || !j.is_object()) {                                                                      \
            _authoredStyle.reset();                                                                               \
            return;                                                                                               \
        }                                                                                                         \
        TStyle style{};                                                                                           \
        ReflectionSerializer::deserializeByRuntimeReflection(style, j, "");                                       \
        _authoredStyle = std::move(style);                                                                        \
    }

} // namespace ya
