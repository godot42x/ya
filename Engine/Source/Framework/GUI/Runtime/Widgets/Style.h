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
#include "GUI/Widgets/Brush.h"
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

    bool operator==(const FWidgetStyle&) const = default;
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

    bool operator==(const FTextStyle&) const = default;
};

/// Panel: fill brush (solid color, or image later). Mirrors UIPanel default.
struct FPanelStyle
{
    FBrush fillColor = FBrush::Solid({0.2f, 0.2f, 0.2f, 0.8f});

    bool operator==(const FPanelStyle&) const = default;
};

/// Button: one fill brush per state + label color + padding. Mirrors
/// UIButton's _normal/_hovered/_pressed/_focused defaults and content padding.
struct FButtonStyle
{
    FBrush     normalFill   = FBrush::Solid({0.8f, 0.8f, 0.8f, 1.0f});
    FBrush     hoveredFill  = FBrush::Solid({0.6f, 0.6f, 0.6f, 1.0f});
    FBrush     pressedFill  = FBrush::Solid({0.4f, 0.4f, 0.4f, 1.0f});
    FBrush     focusedFill  = FBrush::Solid({0.26f, 0.52f, 0.90f, 1.0f});
    FBrush     disabledFill = FBrush::Solid({0.5f, 0.5f, 0.5f, 1.0f});
    glm::vec4  textColor    = {1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec2  padding      = {12.0f, 4.0f};

    bool operator==(const FButtonStyle&) const = default;
};

/// Menu bar item: label + normal/hovered fill. Mirrors UIMenuBarItem.
struct FMenuBarItemStyle
{
    glm::vec4 textColor   = {0.90f, 0.92f, 0.95f, 1.0f};
    FBrush    normalFill  = FBrush::Solid({0.10f, 0.11f, 0.13f, 1.0f});
    FBrush    hoveredFill = FBrush::Solid({0.20f, 0.22f, 0.27f, 1.0f});

    bool operator==(const FMenuBarItemStyle&) const = default;
};

/// Tab strip button: per-state fill brush + accent. Mirrors UITabButton defaults.
struct FTabStyle
{
    glm::vec4 textColor    = {0.90f, 0.92f, 0.95f, 1.0f};
    FBrush    normalFill   = FBrush::Solid({0.15f, 0.16f, 0.19f, 1.0f});
    FBrush    hoveredFill  = FBrush::Solid({0.21f, 0.23f, 0.27f, 1.0f});
    FBrush    selectedFill = FBrush::Solid({0.12f, 0.13f, 0.17f, 1.0f});
    glm::vec4 accentColor  = {0.30f, 0.55f, 0.92f, 1.0f};
    glm::vec2 padding      = {14.0f, 6.0f};

    bool operator==(const FTabStyle&) const = default;
};

/// Split pane divider: one fill brush per state (normal / hovered / dragging).
/// Mirrors UISplitPane's _dividerColor/_dividerHoveredColor/_dividerDraggingColor.
struct FSplitPaneStyle
{
    FBrush dividerFill         = FBrush::Solid({0.11f, 0.12f, 0.15f, 1.0f});
    FBrush dividerHoveredFill  = FBrush::Solid({0.26f, 0.31f, 0.40f, 1.0f});
    FBrush dividerDraggingFill = FBrush::Solid({0.32f, 0.55f, 0.92f, 1.0f});

    bool operator==(const FSplitPaneStyle&) const = default;
};

/// Scroll bar: track + thumb fill brushes and thickness. Mirrors
/// UIScrollViewport's _scrollbarTrackColor/_scrollbarThumbColor/_scrollbarWidth.
/// (_bShowScrollbar stays a widget behavior switch, not a style attribute.)
struct FScrollBarStyle
{
    FBrush trackColor = FBrush::Solid({0.10f, 0.11f, 0.14f, 0.9f});
    FBrush thumbColor = FBrush::Solid({0.34f, 0.38f, 0.46f, 1.0f});
    float  width      = 8.0f;

    bool operator==(const FScrollBarStyle&) const = default;
};

/// Dock space canvas + drop preview. The split divider color lives in
/// FSplitPaneStyle (SplitPane is a general control, not dock-specific).
struct FDockSpaceStyle
{
    FBrush canvasColor      = FBrush::Solid({0.075f, 0.082f, 0.10f, 1.0f});
    FBrush dropPreviewColor = FBrush::Solid({0.30f, 0.55f, 0.90f, 0.55f});

    bool operator==(const FDockSpaceStyle&) const = default;
};

/// Floating dock window body / border / edge affordance / title. Mirrors
/// UIDockFloatingWindow and its resize handles.
struct FFloatingWindowStyle
{
    FBrush    bodyFill       = FBrush::Solid({0.145f, 0.150f, 0.180f, 0.985f});
    FBrush    innerFill      = FBrush::Solid({0.08f, 0.09f, 0.12f, 0.55f});
    glm::vec4 borderColor    = {0.27f, 0.30f, 0.38f, 1.0f};
    glm::vec4 edgeAffordance = {0.40f, 0.47f, 0.62f, 0.42f};
    glm::vec4 titleTextColor = {0.90f, 0.92f, 0.95f, 1.0f};
    glm::vec2 minSize        = {220.0f, 160.0f};

    bool operator==(const FFloatingWindowStyle&) const = default;
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
