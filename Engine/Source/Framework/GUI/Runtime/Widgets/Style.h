#pragma once

// ============================================================================
// Style - data-driven widget styling (layout/style separation).
//
// A UIStyleSet holds named styles; each style is a Reactive<FWidgetStyle>, so
// widgets bind a style and a style edit marks every dependent paint-dirty via
// the existing reactive invalidation — "change one style, the whole themed UI
// repaints" without touching per-widget color fields.
//
// Minimal by design: fill/text colors + font size + padding. Richer style
// properties (borders, radii, brushes, 9-patch) are later extensions.
// ============================================================================

#include "Core/Api.h"
#include "GUI/Widgets/Reactive.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <unordered_map>

namespace ya
{

/// Common style attributes. Deliberately small; controls read only the
/// attributes they draw (text ignores fill, buttons ignore text, ...).
struct FWidgetStyle
{
    glm::vec4 fillColor = {0.8f, 0.8f, 0.8f, 1.0f};
    glm::vec4 textColor = {1.0f, 1.0f, 1.0f, 1.0f};
    uint32_t  fontSize  = 16;
    glm::vec2 padding   = {0.0f, 0.0f};

    bool operator==(const FWidgetStyle& other) const
    {
        return fillColor == other.fillColor &&
               textColor == other.textColor &&
               fontSize  == other.fontSize &&
               padding   == other.padding;
    }
};

// ============================================================================
// Typed widget styles (style-system Phase 1).
//
// Each widget family resolves its appearance from ONE typed style structure
// instead of exposing a set of bare color fields. Member defaults ARE the
// framework fallback: a widget with no theme/override falls back to the
// neutral values below, preserving today's look until a UITheme (Phase 2)
// supplies a named override.
//
// Phase 1 only adds the type + fallback. Wiring each control to resolve from
// these (and deleting its bare color fields) happens in Phase 3; the values
// below are copied from each control's current defaults so that migration is
// behavior-preserving.
// ============================================================================

/// Text: color + size. Mirrors UIText's authoring fields.
struct FTextStyle
{
    glm::vec4 textColor = {1.0f, 1.0f, 1.0f, 1.0f};
    uint32_t  fontSize  = 16;

    bool operator==(const FTextStyle& o) const
    {
        return textColor == o.textColor && fontSize == o.fontSize;
    }
};

/// Panel: solid fill. Mirrors UIPanel::getColor default.
struct FPanelStyle
{
    glm::vec4 fillColor = {0.2f, 0.2f, 0.2f, 0.8f};

    bool operator==(const FPanelStyle& o) const { return fillColor == o.fillColor; }
};

/// Button: one fill per state + label color + padding. Mirrors UIButton's
/// _normal/_hovered/_pressed/_focused defaults and content padding.
struct FButtonStyle
{
    glm::vec4 normalFill   = {0.8f, 0.8f, 0.8f, 1.0f};
    glm::vec4 hoveredFill  = {0.6f, 0.6f, 0.6f, 1.0f};
    glm::vec4 pressedFill  = {0.4f, 0.4f, 0.4f, 1.0f};
    glm::vec4 focusedFill  = {0.26f, 0.52f, 0.90f, 1.0f};
    glm::vec4 disabledFill = {0.5f, 0.5f, 0.5f, 1.0f};
    glm::vec4 textColor    = {1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec2 padding      = {12.0f, 4.0f};

    bool operator==(const FButtonStyle& o) const
    {
        return normalFill == o.normalFill &&
               hoveredFill == o.hoveredFill &&
               pressedFill == o.pressedFill &&
               focusedFill == o.focusedFill &&
               disabledFill == o.disabledFill &&
               textColor == o.textColor &&
               padding == o.padding;
    }
};

/// Menu bar item: label + normal/hovered fill. Mirrors UIMenuBarItem.
struct FMenuBarItemStyle
{
    glm::vec4 textColor   = {0.90f, 0.92f, 0.95f, 1.0f};
    glm::vec4 normalFill  = {0.10f, 0.11f, 0.13f, 1.0f};
    glm::vec4 hoveredFill = {0.20f, 0.22f, 0.27f, 1.0f};

    bool operator==(const FMenuBarItemStyle& o) const
    {
        return textColor == o.textColor &&
               normalFill == o.normalFill &&
               hoveredFill == o.hoveredFill;
    }
};

/// Tab strip button: per-state + accent. Mirrors UITabButton defaults.
struct FTabStyle
{
    glm::vec4 textColor    = {0.90f, 0.92f, 0.95f, 1.0f};
    glm::vec4 normalFill   = {0.15f, 0.16f, 0.19f, 1.0f};
    glm::vec4 hoveredFill  = {0.21f, 0.23f, 0.27f, 1.0f};
    glm::vec4 selectedFill = {0.12f, 0.13f, 0.17f, 1.0f};
    glm::vec4 accentColor  = {0.30f, 0.55f, 0.92f, 1.0f};
    glm::vec2 padding      = {14.0f, 6.0f};

    bool operator==(const FTabStyle& o) const
    {
        return textColor == o.textColor &&
               normalFill == o.normalFill &&
               hoveredFill == o.hoveredFill &&
               selectedFill == o.selectedFill &&
               accentColor == o.accentColor &&
               padding == o.padding;
    }
};

/// Dock space canvas + split divider + drop preview.
struct FDockSpaceStyle
{
    glm::vec4 canvasColor        = {0.075f, 0.082f, 0.10f, 1.0f};
    glm::vec4 splitDividerColor  = {0.28f, 0.30f, 0.36f, 1.0f};
    glm::vec4 dropPreviewColor   = {0.30f, 0.55f, 0.90f, 0.55f};

    bool operator==(const FDockSpaceStyle& o) const
    {
        return canvasColor == o.canvasColor &&
               splitDividerColor == o.splitDividerColor &&
               dropPreviewColor == o.dropPreviewColor;
    }
};

/// Floating dock window body / border / edge affordance / title. Mirrors
/// UIDockFloatingWindow and its resize handles.
struct FFloatingWindowStyle
{
    glm::vec4 bodyFill       = {0.145f, 0.150f, 0.180f, 0.985f};
    glm::vec4 innerFill      = {0.08f, 0.09f, 0.12f, 0.55f};
    glm::vec4 borderColor    = {0.27f, 0.30f, 0.38f, 1.0f};
    glm::vec4 edgeAffordance = {0.40f, 0.47f, 0.62f, 0.42f};
    glm::vec4 titleTextColor = {0.90f, 0.92f, 0.95f, 1.0f};
    glm::vec2 minSize        = {220.0f, 160.0f};

    bool operator==(const FFloatingWindowStyle& o) const
    {
        return bodyFill == o.bodyFill &&
               innerFill == o.innerFill &&
               borderColor == o.borderColor &&
               edgeAffordance == o.edgeAffordance &&
               titleTextColor == o.titleTextColor &&
               minSize == o.minSize;
    }
};

/// Named style collection. Styles are Reactive so widgets can bind them and
/// be notified on edit. Owned by the host (or a singleton); not tied to a
/// specific tree so one set themes many widgets/windows.
class YA_GUI_API UIStyleSet
{
public:
    /// Define (or replace) a named style and return its reactive handle.
    std::shared_ptr<Reactive<FWidgetStyle>> define(std::string name, FWidgetStyle style);

    /// Find a style by name; null when undefined.
    [[nodiscard]] std::shared_ptr<Reactive<FWidgetStyle>> find(const std::string& name) const;

    /// Bind a style to a widget. Implemented in .cpp (needs UIElement).
    void bindTo(std::shared_ptr<Reactive<FWidgetStyle>> style, struct UIElement& widget);

private:
    std::unordered_map<std::string, std::shared_ptr<Reactive<FWidgetStyle>>> _styles;
};

} // namespace ya
