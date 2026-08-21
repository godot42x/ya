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

} // namespace ya
