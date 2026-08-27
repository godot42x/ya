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

#include "Core/Log.h"
#include "Core/Reflection/ReflectionSerializer.h"

#include <string>
#include <type_traits>
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
    if (!tree) {
        return nullptr;
    }
    // Unconditional generation edge: a theme switch must repaint this widget
    // even when the resolved style value itself never changed.
    tree->getThemeGeneration()->get(level);
    if (!tree->getTheme()) {
        return nullptr;
    }
    if (auto style = tree->getTheme()->find<TStyle>(key)) {
        return &style->get(level);
    }
    return nullptr;
}

template <typename TStyle>
const TStyle* peekThemeStyle(const UIElement& widget, const std::string& key)
{
    WidgetTree* tree = widget.getTree();
    if (!tree || !tree->getTheme()) {
        return nullptr;
    }
    if (auto style = tree->getTheme()->find<TStyle>(key)) {
        return &style->value();
    }
    return nullptr;
}

[[nodiscard]] inline bool isStylePatchEmpty(const nlohmann::json& patch)
{
    return patch.is_null() || !patch.is_object() || patch.empty();
}

/// True when `patch` contains every reflected (serializable) field of TStyle.
/// A complete patch is a full freeze: resolve skips theme edges.
template <typename TStyle>
[[nodiscard]] inline bool stylePatchCoversAllFields(const nlohmann::json& patch)
{
    if (isStylePatchEmpty(patch) || !patch.is_object()) {
        return false;
    }
    ::ya::reflection::DeferredInitializerQueue::instance().executeAll();
    auto* cls = ClassRegistry::instance().getClass(ya::type_index_v<TStyle>);
    if (!cls) {
        return false;
    }
    bool bComplete = true;
    cls->visitOwnProperties([&](const std::string& name, const Property& prop) {
        if (prop.metadata.hasFlag(FieldFlags::NotSerialized)) {
            return;
        }
        if (!patch.contains(name)) {
            bComplete = false;
        }
    });
    return bComplete;
}

template <typename TStyle>
[[nodiscard]] inline TStyle mergeStylePatch(TStyle base, const nlohmann::json& patch)
{
    if (isStylePatchEmpty(patch) || !patch.is_object()) {
        return base;
    }
    ::ya::reflection::DeferredInitializerQueue::instance().executeAll();
    auto* cls = ClassRegistry::instance().getClass(ya::type_index_v<TStyle>);
    if (!cls) {
        return base;
    }
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        if (it.key() == "__base__") {
            continue;
        }
        Property* prop = cls->getProperty(it.key());
        if (!prop) {
            continue;
        }
        ReflectionSerializer::deserializeProperty(*prop, &base, it.value());
    }
    return base;
}

template <typename TStyle>
[[nodiscard]] inline bool stylePatchUsesThemeBase(const std::string& key,
                                                  const nlohmann::json& patch)
{
    return !key.empty() && !stylePatchCoversAllFields<TStyle>(patch);
}

template <typename TStyle>
[[nodiscard]] inline TStyle computeResolvedWidgetStyle(const UIElement&          widget,
                                                       const std::string&        key,
                                                       const nlohmann::json&     patch,
                                                       ReactiveBase::EDirtyLevel level,
                                                       bool                      bTrackDependencies)
{
    TStyle base{};
    if (stylePatchUsesThemeBase<TStyle>(key, patch)) {
        const TStyle* themed = bTrackDependencies
                                   ? resolveThemeStyle<TStyle>(widget, key, level)
                                   : peekThemeStyle<TStyle>(widget, key);
        if (themed) {
            base = *themed;
        }
    }
    return mergeStylePatch(std::move(base), patch);
}

/// Sparse overlay resolve: theme (when the patch does not cover every
/// reflected field) + per-key deserializeProperty onto that base.
/// A complete patch is a full freeze — same as historical setStyle(TStyle):
/// no theme-generation edge, base is TStyle{}. `key` defaults to
/// `widget._styleKey`. Uncached; widget paint/layout should use
/// `UIStyledWidget::resolvedStyle()` instead.
template <typename TStyle>
TStyle resolveWidgetStyle(const UIElement&                 widget,
                          const std::string&               key,
                          const nlohmann::json&            patch,
                          ReactiveBase::EDirtyLevel        level = ReactiveBase::EDirtyLevel::Paint)
{
    return computeResolvedWidgetStyle<TStyle>(widget, key, patch, level, true);
}

template <typename TStyle>
TStyle resolveWidgetStyle(const UIElement&          widget,
                          const nlohmann::json&     patch,
                          ReactiveBase::EDirtyLevel level = ReactiveBase::EDirtyLevel::Paint)
{
    return resolveWidgetStyle<TStyle>(widget, widget._styleKey, patch, level);
}

template <typename TStyle>
TStyle resolveWidgetStyle(const UIElement&          widget,
                          ReactiveBase::EDirtyLevel level = ReactiveBase::EDirtyLevel::Paint)
{
    return resolveWidgetStyle<TStyle>(widget, widget._styleKey, nlohmann::json{}, level);
}

/// Per-widget sparse style patch (JSON object, keys = reflected TStyle
/// field names). Empty / null / `{}` means inherit the whole style from
/// the theme (or TStyle{}). `setStyle(TStyle)` writes every field (full
/// freeze). `setStyleField` writes one key and keeps theme edges.
///
/// `_resolvedStyleCache` is the dense merge result. Paint/layout read
/// `resolvedStyle()`; merge runs on dirty/recompute, not every paint.
/// The cache is not persisted.
template <typename TWidget, typename TStyle>
struct UIStyledWidget
{
    nlohmann::json _authoredStyle;

  protected:
    mutable TStyle                    _resolvedStyleCache{};
    mutable WidgetTree*               _resolvedStyleTree          = nullptr;
    mutable std::string               _resolvedStyleKeySnapshot;
    mutable uint64_t                  _resolvedStyleGeneration    = 0;
    mutable ReactiveBase::EDirtyLevel _resolvedStyleLevel         = ReactiveBase::EDirtyLevel::Paint;
    mutable bool                      _bResolvedStyleCacheValid   = false;
    mutable bool                      _bResolvedStyleCacheDirty   = true;
    mutable bool                      _bResolvedStyleUsesThemeBase = false;

    void invalidateResolvedStyleCache() const
    {
        _bResolvedStyleCacheValid    = false;
        _bResolvedStyleCacheDirty    = true;
        _bResolvedStyleUsesThemeBase = false;
    }

    template <typename TFallback>
    [[nodiscard]] const TStyle& resolvedStyleCache(const TWidget&               widget,
                                                   ReactiveBase::EDirtyLevel    level,
                                                   TFallback&&                  applyFallback,
                                                   bool                         bTrackDependencies = true) const
    {
        WidgetTree*     tree       = widget.getTree();
        const uint64_t  generation = tree ? tree->getThemeGeneration()->value() : 0;
        const bool      bNeedStrongerLevel = level == ReactiveBase::EDirtyLevel::Layout &&
                                        _resolvedStyleLevel != ReactiveBase::EDirtyLevel::Layout;
        const bool      bWidgetDirty = widget.isPaintDirty();
        const bool      bGenerationChanged = _bResolvedStyleUsesThemeBase &&
                                        (_resolvedStyleTree != tree || _resolvedStyleGeneration != generation);
        const bool      bKeyChanged = _resolvedStyleTree != tree || _resolvedStyleKeySnapshot != widget._styleKey;
        const bool      bNeedRecompute = !_bResolvedStyleCacheValid || _bResolvedStyleCacheDirty ||
                                    bNeedStrongerLevel || bWidgetDirty || bGenerationChanged || bKeyChanged;

        if (bNeedRecompute) {
            _resolvedStyleCache = computeResolvedWidgetStyle<TStyle>(widget, widget._styleKey, _authoredStyle, level, bTrackDependencies);
            applyFallback(_resolvedStyleCache);
            _resolvedStyleTree           = tree;
            _resolvedStyleKeySnapshot    = widget._styleKey;
            _resolvedStyleGeneration     = generation;
            _resolvedStyleLevel          = level;
            _bResolvedStyleCacheValid    = true;
            _bResolvedStyleCacheDirty    = false;
            _bResolvedStyleUsesThemeBase = stylePatchUsesThemeBase<TStyle>(widget._styleKey, _authoredStyle);
        }
        else if (bTrackDependencies && _bResolvedStyleUsesThemeBase) {
            (void)resolveThemeStyle<TStyle>(widget, widget._styleKey, level);
        }
        return _resolvedStyleCache;
    }

    [[nodiscard]] const TStyle& resolvedStyleCache(const TWidget&            widget,
                                                   ReactiveBase::EDirtyLevel level = ReactiveBase::EDirtyLevel::Paint,
                                                   bool                      bTrackDependencies = true) const
    {
        return resolvedStyleCache(widget, level, [](TStyle&) {}, bTrackDependencies);
    }

  public:
    [[nodiscard]] const TStyle& resolvedStyle(ReactiveBase::EDirtyLevel level = ReactiveBase::EDirtyLevel::Paint,
                                              bool                      bTrackDependencies = true) const
    {
        return resolvedStyleCache(static_cast<const TWidget&>(*this), level, bTrackDependencies);
    }


    void setStyle(TStyle style, EUIPropertyImpact impact = EUIPropertyImpact::Layout)
    {
        auto& self = static_cast<TWidget&>(*this);
        ::ya::reflection::DeferredInitializerQueue::instance().executeAll();
        nlohmann::json next = ReflectionSerializer::serializeByRuntimeReflection(style);
        if (!next.is_object()) {
            next = nlohmann::json::object();
        }
        if (_authoredStyle == next) {
            return;
        }
        _authoredStyle = std::move(next);
        invalidateResolvedStyleCache();
        self.invalidateProperty(impact);
    }

    template <typename TValue>
    void setStyleField(std::string name, const TValue& value, EUIPropertyImpact impact = EUIPropertyImpact::Paint)
    {
        auto& self = static_cast<TWidget&>(*this);
        ::ya::reflection::DeferredInitializerQueue::instance().executeAll();
        auto* cls = ClassRegistry::instance().getClass(ya::type_index_v<TStyle>);
        if (!cls) {
            YA_CORE_WARN("UIStyledWidget: no reflection for style type, cannot set field '{}'", name);
            return;
        }
        Property* prop = cls->getProperty(name);
        if (!prop) {
            YA_CORE_WARN("UIStyledWidget: unknown style field '{}'", name);
            return;
        }

        nlohmann::json fieldJson;
        if constexpr (std::is_same_v<std::remove_cvref_t<TValue>, nlohmann::json>) {
            fieldJson = value;
        }
        else {
            TStyle tmp{};
            void*  addr = prop->getMutableAddress(&tmp);
            if (!addr) {
                YA_CORE_WARN("UIStyledWidget: cannot address style field '{}'", name);
                return;
            }
            *static_cast<std::remove_cvref_t<TValue>*>(addr) = value;
            fieldJson = ReflectionSerializer::serializeProperty(&tmp, *prop);
        }

        if (!_authoredStyle.is_object()) {
            _authoredStyle = nlohmann::json::object();
        }
        if (_authoredStyle.contains(name) && _authoredStyle[name] == fieldJson) {
            return;
        }
        _authoredStyle[name] = std::move(fieldJson);
        invalidateResolvedStyleCache();
        self.invalidateProperty(impact);
    }

    void clearStyleField(const std::string& name, EUIPropertyImpact impact = EUIPropertyImpact::Layout)
    {
        auto& self = static_cast<TWidget&>(*this);
        if (!_authoredStyle.is_object() || !_authoredStyle.contains(name)) {
            return;
        }
        _authoredStyle.erase(name);
        invalidateResolvedStyleCache();
        self.invalidateProperty(impact);
    }

    void clearAuthoredStyle()
    {
        auto& self = static_cast<TWidget&>(*this);
        if (isStylePatchEmpty(_authoredStyle)) {
            return;
        }
        _authoredStyle = nlohmann::json{};
        invalidateResolvedStyleCache();
        self.invalidateProperty(EUIPropertyImpact::Layout);
    }

    [[nodiscard]] bool hasAuthoredStyle() const
    {
        return _authoredStyle.is_object() && !_authoredStyle.empty();
    }
};

/// Persist `_authoredStyle` through UIElement's virtual serialize hook.
/// Mixin fields cannot be YA_REFLECT_FIELD'd from a UIElement* (MI offset).
/// Empty / null / `{}` omits the key; a JSON object is the sparse patch
/// (missing keys inherit). Old full-object documents remain a full freeze.
#define YA_GUI_AUTHORED_STYLE_IO(TStyle)                                                                          \
    [[nodiscard]] nlohmann::json serializeAuthoredStyle() const override                                          \
    {                                                                                                             \
        static_assert(std::is_same_v<std::remove_cvref_t<decltype(_authoredStyle)>, nlohmann::json>);              \
        (void)sizeof(TStyle);                                                                                     \
        if (!_authoredStyle.is_object() || _authoredStyle.empty()) {                                              \
            return nullptr;                                                                                       \
        }                                                                                                         \
        return _authoredStyle;                                                                                    \
    }                                                                                                             \
    void deserializeAuthoredStyle(const nlohmann::json& j) override                                               \
    {                                                                                                             \
        if (j.is_null() || !j.is_object()) {                                                                      \
            _authoredStyle = nlohmann::json{};                                                                    \
            invalidateResolvedStyleCache();                                                                       \
            return;                                                                                               \
        }                                                                                                         \
        _authoredStyle = j;                                                                                       \
        invalidateResolvedStyleCache();                                                                           \
    }

} // namespace ya
