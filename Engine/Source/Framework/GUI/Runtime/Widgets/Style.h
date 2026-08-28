#pragma once

// ============================================================================
// Style - data-driven widget styling (layout/style separation, style-system
// Phase 1+).
//
// A UIStyleSet holds named typed styles; each style is a Reactive<TStyle>
// (bucketed by type), so a style edit marks every widget that read it
// paint-/layout-dirty via the existing reactive invalidation — "change one
// style, the whole themed UI repaints" without touching per-widget color
// fields. Widgets resolve typed styles through UITheme + resolveThemeStyle
// (Theme.h); the legacy FWidgetStyle/bindTo/bindStyle path was removed in
// the Phase 3 cleanup (unified binding path: paint-time get()).
// ============================================================================

#include "Core/Api.h"
#include "GUI/Widgets/Brush.h"
#include "GUI/Binding/Reactive.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>

namespace ya
{

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

/// Text: color + size, plus the optional themed background (badge/chip) fill
/// + padding. Mirrors what UIText's paint actually consumes (authoring
/// fields + the _bFillBackground background); the background is off unless
/// the widget opts in via _bFillBackground.
struct FTextStyle
{
    glm::vec4 textColor = {1.0f, 1.0f, 1.0f, 1.0f};
    uint32_t  fontSize  = 16;
    FBrush    fillColor = FBrush::solid({0.8f, 0.8f, 0.8f, 1.0f});
    glm::vec2 padding   = {0.0f, 0.0f};

    bool operator==(const FTextStyle&) const = default;
};

/// Panel: fill brush (solid color, or image later). Mirrors UIPanel default.
struct FPanelStyle
{
    FBrush fillColor = FBrush::solid({0.2f, 0.2f, 0.2f, 0.8f});

    bool operator==(const FPanelStyle&) const = default;
};

/// Button: one fill brush per state + label color + padding. Mirrors
/// UIButton's _normal/_hovered/_pressed/_focused defaults and content padding.
struct FButtonStyle
{
    FBrush     normalFill   = FBrush::solid({0.8f, 0.8f, 0.8f, 1.0f});
    FBrush     hoveredFill  = FBrush::solid({0.6f, 0.6f, 0.6f, 1.0f});
    FBrush     pressedFill  = FBrush::solid({0.4f, 0.4f, 0.4f, 1.0f});
    FBrush     focusedFill  = FBrush::solid({0.26f, 0.52f, 0.90f, 1.0f});
    FBrush     disabledFill = FBrush::solid({0.5f, 0.5f, 0.5f, 1.0f});
    glm::vec4  textColor    = {1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec2  padding      = {12.0f, 4.0f};

    bool operator==(const FButtonStyle&) const = default;
};

/// Menu bar item: label + normal/hovered fill. Mirrors UIMenuBarItem.
struct FMenuBarItemStyle
{
    glm::vec4 textColor   = {0.90f, 0.92f, 0.95f, 1.0f};
    FBrush    normalFill  = FBrush::solid({0.10f, 0.11f, 0.13f, 1.0f});
    FBrush    hoveredFill = FBrush::solid({0.20f, 0.22f, 0.27f, 1.0f});
    glm::vec4 separatorColor = {0.28f, 0.30f, 0.36f, 1.0f};

    bool operator==(const FMenuBarItemStyle&) const = default;
};

/// Tab strip button: per-state fill brush + accent + strip chrome (bottom
/// separator rule, empty-zone placeholder). Mirrors UITabButton/UITabBar
/// paint defaults.
struct FTabStyle
{
    glm::vec4 textColor    = {0.90f, 0.92f, 0.95f, 1.0f};
    FBrush    normalFill   = FBrush::solid({0.15f, 0.16f, 0.19f, 1.0f});
    FBrush    hoveredFill  = FBrush::solid({0.21f, 0.23f, 0.27f, 1.0f});
    FBrush    selectedFill = FBrush::solid({0.12f, 0.13f, 0.17f, 1.0f});
    glm::vec4 accentColor  = {0.30f, 0.55f, 0.92f, 1.0f};
    glm::vec2 padding      = {14.0f, 6.0f};
    /// Bottom rule separating the strip from the content host below it.
    glm::vec4 separatorColor      = {0.28f, 0.30f, 0.36f, 1.0f};
    /// Muted placeholder label when the bar hosts no tabs (empty zone hint).
    glm::vec4 placeholderTextColor = {0.45f, 0.48f, 0.55f, 1.0f};

    bool operator==(const FTabStyle&) const = default;
};

/// Split pane divider: one fill brush per state (normal / hovered / dragging).
/// These defaults are the framework fallback for UISplitPane's divider (the old
    /// bare _dividerColor/_dividerHoveredColor/_dividerDraggingColor fields were
    /// deleted in the Phase 3 cleanup).
struct FSplitPaneStyle
{
    FBrush dividerFill         = FBrush::solid({0.11f, 0.12f, 0.15f, 1.0f});
    FBrush dividerHoveredFill  = FBrush::solid({0.26f, 0.31f, 0.40f, 1.0f});
    FBrush dividerDraggingFill = FBrush::solid({0.32f, 0.55f, 0.92f, 1.0f});

    bool operator==(const FSplitPaneStyle&) const = default;
};

/// Scroll bar: track + thumb fill brushes and thickness. Mirrors
/// These defaults are the framework fallback for UIScrollViewport's scrollbar (the
    /// old bare _scrollbarTrackColor/_scrollbarThumbColor/_scrollbarWidth fields were
    /// deleted in the Phase 3 cleanup).
/// (_bShowScrollbar stays a widget behavior switch, not a style attribute.)
struct FScrollBarStyle
{
    FBrush trackColor = FBrush::solid({0.10f, 0.11f, 0.14f, 0.9f});
    FBrush thumbColor = FBrush::solid({0.34f, 0.38f, 0.46f, 1.0f});
    float  width      = 8.0f;

    bool operator==(const FScrollBarStyle&) const = default;
};

/// Dock space canvas + drop preview (merge vs split states + outline). The
/// split divider color lives in FSplitPaneStyle (SplitPane is a general
/// control, not dock-specific).
struct FDockSpaceStyle
{
    FBrush canvasColor          = FBrush::solid({0.075f, 0.082f, 0.10f, 1.0f});
    FBrush dropPreviewColor     = FBrush::solid({0.28f, 0.52f, 0.90f, 0.28f});
    /// Preview highlight when the drop merges into an existing leaf.
    FBrush dropPreviewMergeColor = FBrush::solid({0.26f, 0.76f, 0.46f, 0.28f});
    glm::vec4 dropPreviewOutlineColor = {0.34f, 0.60f, 0.96f, 1.0f};

    bool operator==(const FDockSpaceStyle&) const = default;
};

/// Floating dock window body / border / edge affordance / title. Mirrors
/// UIDockFloatingWindow and its resize handles.
struct FFloatingWindowStyle
{
    FBrush    bodyFill       = FBrush::solid({0.145f, 0.150f, 0.180f, 0.985f});
    FBrush    innerFill      = FBrush::solid({0.08f, 0.09f, 0.12f, 0.55f});
    glm::vec4 borderColor    = {0.27f, 0.30f, 0.38f, 1.0f};
    glm::vec4 edgeAffordance = {0.40f, 0.47f, 0.62f, 0.42f};
    glm::vec4 titleTextColor = {0.90f, 0.92f, 0.95f, 1.0f};
    glm::vec2 minSize        = {220.0f, 160.0f};

    bool operator==(const FFloatingWindowStyle&) const = default;
};

/// Hierarchy / project tree: row fills + label + expand arrow. Geometry
/// (rowHeight / indent / arrow width) stays on the widget — those are layout
/// behavior, not look.
struct FTreeViewStyle
{
    glm::vec4 textColor         = {0.90f, 0.92f, 0.95f, 1.0f};
    FBrush    selectedFill      = FBrush::solid({0.22f, 0.42f, 0.78f, 1.0f});
    FBrush    hoveredFill       = FBrush::solid({0.24f, 0.26f, 0.31f, 1.0f});
    glm::vec4 arrowColor        = {0.60f, 0.65f, 0.70f, 1.0f};
    FBrush    arrowHoveredFill  = FBrush::solid({0.32f, 0.36f, 0.44f, 1.0f});
    glm::vec4 dropIndicator     = {0.22f, 0.42f, 0.78f, 1.0f};
    uint32_t  fontSize          = 14;

    bool operator==(const FTreeViewStyle&) const = default;
};

/// Single-line text field: fill, caret, text. Authoring fontSize on the
/// widget is layout/behavior; look comes from this style when themed.
struct FTextFieldStyle
{
    FBrush    backgroundFill = FBrush::solid({0.08f, 0.09f, 0.12f, 1.0f});
    glm::vec4 textColor      = {1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4 caretColor     = {0.90f, 0.92f, 0.95f, 1.0f};
    uint32_t  fontSize       = 16;

    bool operator==(const FTextFieldStyle&) const = default;
};

/// Popup menu panel + item rows. Items resolve this key at paint; the panel
/// uses `menu.panel` as an FPanelStyle so it stays a regular panel.
struct FMenuStyle
{
    FBrush    itemNormalFill  = FBrush::solid({0.13f, 0.14f, 0.17f, 1.0f});
    FBrush    itemHoveredFill = FBrush::solid({0.22f, 0.42f, 0.78f, 1.0f});
    glm::vec4 textColor       = {0.90f, 0.92f, 0.95f, 1.0f};
    glm::vec4 iconColor       = {0.78f, 0.82f, 0.88f, 1.0f};
    glm::vec4 checkmarkColor  = {0.30f, 0.76f, 0.46f, 1.0f};
    glm::vec4 shortcutColor   = {0.60f, 0.65f, 0.72f, 1.0f};
    glm::vec4 disabledTextColor = {0.46f, 0.50f, 0.58f, 1.0f};
    glm::vec4 disabledIconColor = {0.42f, 0.46f, 0.54f, 1.0f};
    glm::vec4 separatorColor  = {0.28f, 0.30f, 0.36f, 1.0f};
    glm::vec4 submenuArrowColor = {0.60f, 0.65f, 0.72f, 1.0f};
    uint32_t  fontSize        = 13;

    bool operator==(const FMenuStyle&) const = default;
};

/// List / tree row chrome. Normal fill defaults to transparent so a row on
/// a themed panel does not stamp a second surface.
struct FSelectableRowStyle
{
    FBrush normalFill          = FBrush::solid({0.16f, 0.17f, 0.20f, 0.0f});
    FBrush hoveredFill         = FBrush::solid({0.24f, 0.26f, 0.31f, 1.0f});
    FBrush selectedFill        = FBrush::solid({0.22f, 0.42f, 0.78f, 1.0f});
    FBrush selectedHoveredFill = FBrush::solid({0.30f, 0.50f, 0.86f, 1.0f});

    bool operator==(const FSelectableRowStyle&) const = default;
};

/// Inspector numeric drag: fill, dragging fill, text, outline.
struct FDragFloatStyle
{
    FBrush    backgroundFill = FBrush::solid({0.17f, 0.19f, 0.24f, 1.0f});
    FBrush    draggingFill   = FBrush::solid({0.18f, 0.24f, 0.34f, 1.0f});
    glm::vec4 textColor      = {0.90f, 0.92f, 0.95f, 1.0f};
    glm::vec4 borderColor    = {0.30f, 0.33f, 0.40f, 1.0f};
    uint32_t  fontSize       = 13;

    bool operator==(const FDragFloatStyle&) const = default;
};

/// Check box: unchecked / hovered / checked fill + the check-mark tint.
/// Box edge length stays on the widget.
struct FCheckBoxStyle
{
    FBrush    boxFill      = FBrush::solid({0.55f, 0.60f, 0.68f, 1.0f});
    FBrush    hoveredFill  = FBrush::solid({0.34f, 0.38f, 0.46f, 1.0f});
    FBrush    checkedFill  = FBrush::solid({0.24f, 0.46f, 0.82f, 1.0f});
    glm::vec4 checkColor   = {0.95f, 0.96f, 0.98f, 1.0f};

    bool operator==(const FCheckBoxStyle&) const = default;
};

/// Collapsed combo field. The popup list is a UIMenu and uses FMenuStyle.
struct FComboBoxStyle
{
    FBrush    fieldFill   = FBrush::solid({0.16f, 0.18f, 0.22f, 1.0f});
    FBrush    hoveredFill = FBrush::solid({0.22f, 0.25f, 0.30f, 1.0f});
    glm::vec4 textColor   = {0.90f, 0.92f, 0.95f, 1.0f};
    glm::vec4 arrowColor  = {0.68f, 0.72f, 0.78f, 1.0f};
    uint32_t  fontSize    = 13;

    bool operator==(const FComboBoxStyle&) const = default;
};

/// Horizontal slider track / value fill / thumb. Thumb size stays on the widget.
struct FSliderStyle
{
    FBrush trackFill = FBrush::solid({0.14f, 0.16f, 0.20f, 1.0f});
    FBrush valueFill = FBrush::solid({0.24f, 0.46f, 0.82f, 1.0f});
    FBrush thumbFill = FBrush::solid({0.88f, 0.90f, 0.94f, 1.0f});

    bool operator==(const FSliderStyle&) const = default;
};

/// Table / grid chrome. Row height and column widths stay on the widget.
struct FTableGridStyle
{
    FBrush    backgroundFill  = FBrush::solid({0.12f, 0.13f, 0.16f, 1.0f});
    FBrush    selectedFill    = FBrush::solid({0.22f, 0.42f, 0.78f, 1.0f});
    FBrush    hoveredFill     = FBrush::solid({0.24f, 0.26f, 0.31f, 1.0f});
    glm::vec4 textColor       = {0.90f, 0.92f, 0.95f, 1.0f};
    glm::vec4 headerTextColor = {0.62f, 0.66f, 0.72f, 1.0f};
    glm::vec4 gridColor       = {0.20f, 0.22f, 0.27f, 1.0f};
    uint32_t  fontSize        = 13;

    bool operator==(const FTableGridStyle&) const = default;
};

/// Spin box: field + step buttons.
struct FSpinBoxStyle
{
    FBrush    backgroundFill    = FBrush::solid({0.17f, 0.19f, 0.24f, 1.0f});
    FBrush    buttonFill        = FBrush::solid({0.22f, 0.24f, 0.30f, 1.0f});
    FBrush    buttonHoveredFill = FBrush::solid({0.30f, 0.33f, 0.40f, 1.0f});
    glm::vec4 textColor         = {0.90f, 0.92f, 0.95f, 1.0f};
    glm::vec4 borderColor       = {0.30f, 0.33f, 0.40f, 1.0f};
    uint32_t  fontSize          = 13;

    bool operator==(const FSpinBoxStyle&) const = default;
};

/// Radio: hover row fill + outer/inner dots + label.
struct FRadioButtonStyle
{
    FBrush    hoveredFill = FBrush::solid({0.24f, 0.26f, 0.31f, 1.0f});
    glm::vec4 dotColor    = {0.88f, 0.90f, 0.94f, 1.0f};
    glm::vec4 dotFillColor = {0.24f, 0.46f, 0.82f, 1.0f};
    glm::vec4 textColor   = {0.90f, 0.92f, 0.95f, 1.0f};
    uint32_t  fontSize    = 13;

    bool operator==(const FRadioButtonStyle&) const = default;
};

/// Color edit chrome (swatch background / channel highlight). The edited
/// `_color` is the control value, not a style field.
struct FColorEditStyle
{
    FBrush    backgroundFill    = FBrush::solid({0.12f, 0.13f, 0.17f, 1.0f});
    glm::vec4 textColor         = {0.90f, 0.92f, 0.95f, 1.0f};
    glm::vec4 channelHighlight  = {0.24f, 0.46f, 0.82f, 1.0f};
    uint32_t  fontSize          = 13;

    bool operator==(const FColorEditStyle&) const = default;
};

/// Search combo collapsed field. The popup is a UIMenu (FMenuStyle).
struct FSearchComboStyle
{
    FBrush    backgroundFill = FBrush::solid({0.12f, 0.13f, 0.17f, 1.0f});
    FBrush    hoveredFill    = FBrush::solid({0.24f, 0.26f, 0.31f, 1.0f});
    glm::vec4 textColor      = {0.90f, 0.92f, 0.95f, 1.0f};
    glm::vec4 caretColor     = {0.90f, 0.92f, 0.95f, 1.0f};
    uint32_t  fontSize       = 13;

    bool operator==(const FSearchComboStyle&) const = default;
};

/// Image placeholder (unresolved asset / empty live texture). `_tint` on
/// UIImage is content, not chrome.
struct FImageStyle
{
    FBrush placeholderFill = FBrush::solid({0.24f, 0.26f, 0.31f, 1.0f});

    bool operator==(const FImageStyle&) const = default;
};

/// Popup/modal shield. Non-modal popups paint nothing; modal uses modalFill.
struct FPopupStyle
{
    FBrush modalFill = FBrush::solid({0.0f, 0.0f, 0.0f, 0.45f});

    bool operator==(const FPopupStyle&) const = default;
};

/// Drag source / drop target chrome. Sources use key `drag.source`, targets
/// `drag.target`. `activeFill` is pressed (source) or drop-highlight (target).
struct FDragDropStyle
{
    FBrush    normalFill = FBrush::solid({0.20f, 0.22f, 0.27f, 1.0f});
    FBrush    activeFill = FBrush::solid({0.18f, 0.24f, 0.34f, 1.0f});
    glm::vec4 textColor  = {0.90f, 0.92f, 0.95f, 1.0f};
    uint32_t  fontSize   = 13;

    bool operator==(const FDragDropStyle&) const = default;
};

/// Named style collection. Styles are Reactive so widgets can bind them and
/// be notified on edit. Owned by the host (or a singleton); not tied to a
/// specific tree so one set themes many widgets/windows.
///
/// Generic over the style type: define<FButtonStyle>("button", ...) and
/// define<FTabStyle>("button", ...) coexist (names are bucketed by type_index).
class YA_GUI_API UIStyleSet
{
public:
    /// Define (or replace) a named typed style and return its reactive handle.
    /// Re-defining an existing name of the SAME type mutates the SAME handle
    /// (G4: set() notifies dependents) instead of replacing it — a replaced
    /// handle would silently orphan all existing bindings.
    template <typename TStyle>
    std::shared_ptr<Reactive<TStyle>> define(std::string name, TStyle style)
    {
        auto& bucket = _styles[std::type_index(typeid(TStyle))];
        if (const auto it = bucket.find(name); it != bucket.end()) {
            auto handle = std::static_pointer_cast<Reactive<TStyle>>(it->second);
            handle->set(std::move(style));
            return handle;
        }
        auto handle = std::make_shared<Reactive<TStyle>>(std::move(style));
        bucket[std::move(name)] = handle;
        return handle;
    }

    /// Find a typed style by name; null when undefined.
    template <typename TStyle>
    [[nodiscard]] std::shared_ptr<Reactive<TStyle>> find(const std::string& name) const
    {
        const auto it = _styles.find(std::type_index(typeid(TStyle)));
        if (it == _styles.end()) {
            return nullptr;
        }
        const auto jt = it->second.find(name);
        return jt != it->second.end() ? std::static_pointer_cast<Reactive<TStyle>>(jt->second) : nullptr;
    }

private:
    std::unordered_map<std::type_index,
                       std::unordered_map<std::string, std::shared_ptr<ReactiveBase>>> _styles;
};

} // namespace ya
