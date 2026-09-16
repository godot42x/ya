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
#include "Core/TypeIndex.h"
#include "GUI/Widgets/Brush.h"
#include "GUI/Binding/Reactive.h"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>

namespace ya
{

struct Font;

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

/// Interactive chrome flags. Widgets compose these from input/focus/enabled
/// /selection/validation/drop; `resolveVisualFill` maps the set to one brush.
enum class EWidgetVisualFlag : uint16_t
{
    Hovered    = 1u << 0,
    Pressed    = 1u << 1,
    Focused    = 1u << 2,
    Disabled   = 1u << 3,
    Selected   = 1u << 4,
    Error      = 1u << 5,
    DropTarget = 1u << 6,
};

using EWidgetVisualFlags = uint16_t;

[[nodiscard]] constexpr bool hasVisualFlag(EWidgetVisualFlags flags, EWidgetVisualFlag bit)
{
    return (flags & static_cast<EWidgetVisualFlags>(bit)) != 0;
}

[[nodiscard]] constexpr EWidgetVisualFlags composeVisualFlags(bool bHovered,
                                                              bool bPressed,
                                                              bool bFocused,
                                                              bool bDisabled,
                                                              bool bSelected,
                                                              bool bError,
                                                              bool bDropTarget)
{
    EWidgetVisualFlags flags = 0;
    if (bHovered) {
        flags |= static_cast<EWidgetVisualFlags>(EWidgetVisualFlag::Hovered);
    }
    if (bPressed) {
        flags |= static_cast<EWidgetVisualFlags>(EWidgetVisualFlag::Pressed);
    }
    if (bFocused) {
        flags |= static_cast<EWidgetVisualFlags>(EWidgetVisualFlag::Focused);
    }
    if (bDisabled) {
        flags |= static_cast<EWidgetVisualFlags>(EWidgetVisualFlag::Disabled);
    }
    if (bSelected) {
        flags |= static_cast<EWidgetVisualFlags>(EWidgetVisualFlag::Selected);
    }
    if (bError) {
        flags |= static_cast<EWidgetVisualFlags>(EWidgetVisualFlag::Error);
    }
    if (bDropTarget) {
        flags |= static_cast<EWidgetVisualFlags>(EWidgetVisualFlag::DropTarget);
    }
    return flags;
}

/// Canonical interactive fill table. Exclusive precedence (highest first):
/// Disabled, DropTarget, Error, Pressed, Selected+Hovered, Selected, Hovered,
/// Focused, Normal. Selected+Hovered is a combination, not a ninth exclusive
/// state; styles that lack a combination brush copy Selected into it.
struct FVisualChrome
{
    FBrush normal;
    FBrush hovered;
    FBrush pressed;
    FBrush focused;
    FBrush disabled;
    FBrush selected;
    FBrush selectedHovered;
    FBrush error;
    FBrush dropTarget;

    bool operator==(const FVisualChrome&) const = default;
};

[[nodiscard]] YA_GUI_API const FBrush& resolveVisualFill(const FVisualChrome& chrome,
                                                         EWidgetVisualFlags    flags);

struct FButtonStyle;
struct FSelectableRowStyle;
struct FCheckBoxStyle;
struct FMenuBarItemStyle;
struct FTabStyle;
struct FComboBoxStyle;
struct FMenuStyle;
struct FTableGridStyle;
struct FTreeViewStyle;
struct FExpanderStyle;

[[nodiscard]] YA_GUI_API FVisualChrome visualChrome(const FButtonStyle& style);
[[nodiscard]] YA_GUI_API FVisualChrome visualChrome(const FSelectableRowStyle& style);
[[nodiscard]] YA_GUI_API FVisualChrome visualChrome(const FCheckBoxStyle& style);
[[nodiscard]] YA_GUI_API FVisualChrome visualChrome(const FMenuBarItemStyle& style);
[[nodiscard]] YA_GUI_API FVisualChrome visualChrome(const FTabStyle& style);
[[nodiscard]] YA_GUI_API FVisualChrome visualChrome(const FComboBoxStyle& style);
[[nodiscard]] YA_GUI_API FVisualChrome visualChrome(const FMenuStyle& style);
[[nodiscard]] YA_GUI_API FVisualChrome visualChrome(const FTableGridStyle& style);
[[nodiscard]] YA_GUI_API FVisualChrome visualChrome(const FTreeViewStyle& style);
[[nodiscard]] YA_GUI_API FVisualChrome visualChrome(const FExpanderStyle& style);

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
    /// Font FAMILY this text resolves against, by registered FName. Empty means
    /// "the engine UI face" - which is what almost every label wants, and what
    /// makes switching the default face a whole-shell change instead of a sweep
    /// over every style key. A non-empty value opts THIS text into another
    /// registered family (e.g. MONO_UI_FONT_NAME for a hex/byte readout that has
    /// to align by column).
    ///
    /// A family is a FONT concern, not a text-size concern: fontSize is a step
    /// on the type scale, family is which design the steps are measured in, and
    /// they change independently.
    std::string fontFamily;

    bool operator==(const FTextStyle&) const = default;
};

/// Resolve the font a text style should draw with: its own family when it names
/// one, else the engine UI face. ONE entry point so paint and measurement cannot
/// disagree about which face (and therefore which metrics) a label uses.
[[nodiscard]] YA_GUI_API std::shared_ptr<Font> resolveTextFont(const FTextStyle& style);

/// Panel / card chrome: one fill brush, which owns the panel's corner radius
/// and edge (`fillColor.cornerRadius` / `.borderColor`). Used by `UIBorder`.
/// Content photos are `UIImage`, not this style.
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
    FBrush     selectedFill = FBrush::solid({0.26f, 0.52f, 0.90f, 1.0f});
    FBrush     errorFill    = FBrush::solid({0.72f, 0.24f, 0.24f, 1.0f});
    FBrush     dropTargetFill = FBrush::solid({0.26f, 0.52f, 0.90f, 1.0f});
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
    glm::vec2 padding      = {8.0f, 2.0f};
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
    /// Body fill, which owns the window radius + edge.
    FBrush    bodyFill       = FBrush::solid({0.145f, 0.150f, 0.180f, 0.985f},
                                             0.0f,
                                             {0.27f, 0.30f, 0.38f, 1.0f});
    FBrush    innerFill      = FBrush::solid({0.08f, 0.09f, 0.12f, 0.55f});
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

/// Folding section header (ImGui TreeNode). `expander.header` is the Framed
/// look used by `CollapsingHeader`, not a second widget type. Geometry
/// (headerHeight / indent / arrow width) stays on the widget.
struct FExpanderStyle
{
    glm::vec4 textColor        = {0.90f, 0.92f, 0.95f, 1.0f};
    /// Framed look: the header bar carries its own radius/edge, so
    /// `expander.header` styles a framed header by filling this brush.
    FBrush    headerFill       = FBrush::solid({0.0f, 0.0f, 0.0f, 0.0f});
    FBrush    hoveredFill      = FBrush::solid({0.24f, 0.26f, 0.31f, 1.0f});
    FBrush    pressedFill      = FBrush::solid({0.20f, 0.22f, 0.27f, 1.0f});
    FBrush    focusedFill      = FBrush::solid({0.26f, 0.52f, 0.90f, 0.35f});
    glm::vec4 arrowColor       = {0.60f, 0.65f, 0.70f, 1.0f};
    FBrush    arrowHoveredFill = FBrush::solid({0.32f, 0.36f, 0.44f, 1.0f});
    /// Unframed nested groups: left rail + header hairline. Framed headers
    /// leave this transparent.
    glm::vec4 guideColor       = {0.40f, 0.44f, 0.52f, 0.55f};
    uint32_t  fontSize         = 13;

    bool operator==(const FExpanderStyle&) const = default;
};

/// Preset type scale. Theme keys consume these (`text` / `text.header` /
/// `text.small` / `text.caption` / `textfield` / `textfield.compact`).
/// Do not scatter `setFontSize(12)` for chrome roles — pick a key.
///
/// The steps are tuned for a PROPORTIONAL UI face at 1:1 device scale (see the
/// host's default `fontPath`):
///   kTitle   20 - page title, the single largest piece of chrome; 28 was a
///                 display size that dominated every screen it appeared on
///   kHeader  16 - section header, one visible step above body text (weight
///                 cannot carry the hierarchy: the atlas has one face, so SIZE
///                 is the only axis available)
///   kBody    14 - body copy, field text, and the default for un-styled text;
///                 16 made a 22px control read as cramped around its own label
///   kSmall   13 - dense rows (tree/menu/table/tool fields); the smallest size
///                 that still renders proportional lowercase at full clarity
///   kCaption 11 - uppercase eyebrows and micro captions, tracking-only roles
namespace gui_type
{
inline constexpr uint32_t kTitle   = 20;
inline constexpr uint32_t kHeader  = 16;
inline constexpr uint32_t kBody    = 14;
inline constexpr uint32_t kSmall   = 13;
inline constexpr uint32_t kCaption = 11;
}

/// Single-line text field: fill, hover, caret, outline. `textfield.compact`
/// is the same chrome at `gui_type::kSmall`. Authoring `setFontSize` patches
/// the style; paint reads `resolvedStyle().fontSize`. Text is clipped to the
/// padded inner rect (caret-follow scroll, no font autosize, no Fill grow).
struct FTextFieldStyle
{
    // Each state brush carries the field's radius and edge, so the border can
    // differ per state (hover/focus lift the edge) without a second field.
    FBrush    backgroundFill = FBrush::solid({0.08f, 0.09f, 0.12f, 1.0f},
                                             0.0f,
                                             {0.48f, 0.52f, 0.60f, 1.0f});
    FBrush    hoveredFill    = FBrush::solid({0.22f, 0.25f, 0.32f, 1.0f},
                                             0.0f,
                                             {0.48f, 0.52f, 0.60f, 1.0f});
    FBrush    errorFill        = FBrush::solid({0.72f, 0.24f, 0.24f, 0.45f},
                                               0.0f,
                                               {0.90f, 0.35f, 0.35f, 1.0f});
    glm::vec4 textColor      = {1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4 caretColor     = {0.90f, 0.92f, 0.95f, 1.0f};
    glm::vec4 selectionColor = {0.24f, 0.46f, 0.82f, 0.45f};
    glm::vec2 padding        = {6.0f, 2.0f};
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
    /// Checkable menu row: `checkmarkColor` fills the box when checked, this
    /// edge outlines it in both states (a hollow box reads as "toggleable").
    glm::vec4 checkmarkColor  = {0.30f, 0.76f, 0.46f, 1.0f};
    glm::vec4 checkBoxBorderColor = {0.44f, 0.48f, 0.56f, 1.0f};
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
    FBrush dropTargetFill      = FBrush::solid({0.30f, 0.50f, 0.86f, 1.0f});
    FBrush errorFill           = FBrush::solid({0.72f, 0.24f, 0.24f, 1.0f});
    FBrush disabledFill        = FBrush::solid({0.16f, 0.17f, 0.20f, 0.35f});

    bool operator==(const FSelectableRowStyle&) const = default;
};

/// Inspector numeric drag: fill, dragging fill, text, outline.
/// `padding` is FramePadding: text sits inside the field, not against the
/// 1px outline. Vec rows still space fields with the parent row's spacing.
struct FDragFloatStyle
{
    FBrush    backgroundFill = FBrush::solid({0.17f, 0.19f, 0.24f, 1.0f},
                                             0.0f,
                                             {0.48f, 0.52f, 0.60f, 1.0f});
    FBrush    hoveredFill    = FBrush::solid({0.28f, 0.32f, 0.40f, 1.0f},
                                             0.0f,
                                             {0.72f, 0.78f, 0.90f, 1.0f});
    FBrush    draggingFill   = FBrush::solid({0.20f, 0.32f, 0.48f, 1.0f},
                                             0.0f,
                                             {0.72f, 0.78f, 0.90f, 1.0f});
    FBrush    errorFill      = FBrush::solid({0.72f, 0.24f, 0.24f, 0.45f},
                                             0.0f,
                                             {0.90f, 0.35f, 0.35f, 1.0f});
    glm::vec4 textColor      = {0.90f, 0.92f, 0.95f, 1.0f};
    glm::vec2 padding       = {6.0f, 2.0f};
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
    FBrush    backgroundFill    = FBrush::solid({0.17f, 0.19f, 0.24f, 1.0f},
                                                0.0f,
                                                {0.48f, 0.52f, 0.60f, 1.0f});
    FBrush    hoveredFill       = FBrush::solid({0.22f, 0.25f, 0.32f, 1.0f},
                                                0.0f,
                                                {0.72f, 0.78f, 0.90f, 1.0f});
    FBrush    buttonFill        = FBrush::solid({0.22f, 0.24f, 0.30f, 1.0f});
    FBrush    buttonHoveredFill = FBrush::solid({0.42f, 0.48f, 0.62f, 1.0f});
    glm::vec4 textColor         = {0.90f, 0.92f, 0.95f, 1.0f};
    uint32_t  fontSize          = 13;

    bool operator==(const FSpinBoxStyle&) const = default;
};

/// Radio: hover row fill + outer/inner dots + label.
struct FRadioButtonStyle
{
    FBrush    hoveredFill = FBrush::solid({0.24f, 0.26f, 0.31f, 1.0f});
    /// Outer circle: fill + edge, so an unchecked radio reads as a hollow ring.
    FBrush    dotColor    = FBrush::solid({0.88f, 0.90f, 0.94f, 1.0f});
    /// Inner core when checked (the control rounds it to a circle itself).
    glm::vec4 dotFillColor = {0.24f, 0.46f, 0.82f, 1.0f};
    glm::vec4 textColor   = {0.90f, 0.92f, 0.95f, 1.0f};
    uint32_t  fontSize    = 13;

    bool operator==(const FRadioButtonStyle&) const = default;
};

/// Color edit host chrome (row gap after the swatch, picker text). Channel
/// cells are `UIDragFloat` and use `FDragFloatStyle` — do not duplicate
/// hover/drag fills here.
struct FColorEditStyle
{
    FBrush    backgroundFill = FBrush::solid({0.12f, 0.13f, 0.17f, 1.0f});
    glm::vec4 textColor      = {0.90f, 0.92f, 0.95f, 1.0f};
    glm::vec2 padding        = {6.0f, 3.0f};
    uint32_t  fontSize       = 13;
    /// Color SWATCH chrome. The swatch's fill is the color being edited, so the
    /// style only owns what surrounds it: the edge and the roundness. This is a
    /// style field rather than a control literal because the swatch sits on an
    /// arbitrary fill - a translucent wash of the widget's text color has
    /// nothing to relate to when the user just picked the same value, and the
    /// edge is what tells them where the swatch ends. Keep it at the STRONG end
    /// of the edge ramp: a swatch is small and its content is arbitrary.
    glm::vec4 swatchBorderColor      = {1.0f, 1.0f, 1.0f, 0.26f};
    glm::vec4 swatchHoverBorderColor = {0.29f, 0.56f, 0.98f, 1.0f};
    float     swatchCornerRadius     = 3.0f;
    float     swatchBorderThickness  = 1.0f;

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
    FBrush errorFill       = FBrush::solid({0.72f, 0.24f, 0.24f, 0.45f});

    bool operator==(const FImageStyle&) const = default;
};

/// Popup overlay catalog type. Modal is input capture only; visual chrome
/// (dim, blur, image) is an app-composed child, not a popup style field.
struct FPopupStyle
{
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

/// Single vocabulary of theme keys. `X(TStyle, Name, "key")` is the source
/// for constexpr StyleKey::* names and the runtime catalog. `editor.<key>`
/// is the same vocabulary (GameEditor overlay), not a second catalog.
#define YA_GUI_STYLE_CATALOG(X)                      \
    X(FPanelStyle, Panel, "panel")                   \
    X(FPanelStyle, PanelWindow, "panel.window")      \
    X(FPanelStyle, PanelTitlebar, "panel.titlebar")  \
    X(FPanelStyle, PanelCanvas, "panel.canvas")      \
    X(FPanelStyle, PanelSidebar, "panel.sidebar")    \
    X(FPanelStyle, PanelSidebarCard, "panel.sidebar.card") \
    X(FPanelStyle, PanelSurface, "panel.surface")    \
    X(FPanelStyle, MenuPanel, "menu.panel")          \
    X(FPanelStyle, Tooltip, "tooltip")               \
    X(FPanelStyle, DragGhost, "drag.ghost")          \
    X(FPanelStyle, Canvas, "canvas")                 \
    X(FButtonStyle, Button, "button")                \
    X(FTextStyle, Text, "text")                      \
    X(FTextStyle, TextHeader, "text.header")         \
    X(FTextStyle, TextMuted, "text.muted")           \
    X(FTextStyle, TextError, "text.error")           \
    X(FTextStyle, TextEyebrow, "text.eyebrow")       \
    X(FTextStyle, TextSmall, "text.small")           \
    X(FTextStyle, TextCaption, "text.caption")       \
    X(FMenuBarItemStyle, MenuBar, "menubar")         \
    X(FTabStyle, Tab, "tab")                         \
    X(FTabStyle, TabSidebar, "tab.sidebar")          \
    X(FTabStyle, TabDock, "tab.dock")                 \
    X(FSplitPaneStyle, Split, "split")               \
    X(FScrollBarStyle, ScrollBar, "scrollbar")       \
    X(FDockSpaceStyle, Dock, "dock")                 \
    X(FFloatingWindowStyle, Floating, "floating")    \
    X(FTreeViewStyle, Tree, "tree")                  \
    X(FExpanderStyle, Expander, "expander")          \
    X(FExpanderStyle, ExpanderHeader, "expander.header") \
    X(FTextFieldStyle, TextField, "textfield")       \
    X(FTextFieldStyle, TextFieldCompact, "textfield.compact") \
    X(FMenuStyle, Menu, "menu")                      \
    X(FSelectableRowStyle, Selectable, "selectable") \
    X(FDragFloatStyle, DragFloat, "dragfloat")       \
    X(FCheckBoxStyle, CheckBox, "checkbox")          \
    X(FCheckBoxStyle, Switch, "switch")              \
    X(FComboBoxStyle, ComboBox, "combobox")          \
    X(FSliderStyle, Slider, "slider")                \
    X(FTableGridStyle, Table, "table")               \
    X(FSpinBoxStyle, SpinBox, "spinbox")             \
    X(FRadioButtonStyle, Radio, "radio")             \
    X(FColorEditStyle, ColorEdit, "coloredit")       \
    X(FSearchComboStyle, SearchCombo, "searchcombo") \
    X(FImageStyle, Image, "image")                   \
    X(FPopupStyle, Popup, "popup")                   \
    X(FDragDropStyle, DragSource, "drag.source")     \
    X(FDragDropStyle, DragTarget, "drag.target")

namespace StyleKey
{
#define YA_GUI_STYLE_KEY_CONST(Type, Name, Str) inline constexpr std::string_view Name = Str;
YA_GUI_STYLE_CATALOG(YA_GUI_STYLE_KEY_CONST)
#undef YA_GUI_STYLE_KEY_CONST
}

enum class EStyleKeyLookup : uint8_t
{
    Known,
    Empty,
    UnknownKey,
    TypeMismatch,
};

struct StyleCatalogDiagnostics
{
    uint64_t unknownKeys     = 0;
    uint64_t typeMismatches  = 0;
};

[[nodiscard]] YA_GUI_API EStyleKeyLookup lookupStyleKey(std::string_view key, type_index_t styleType);
[[nodiscard]] YA_GUI_API StyleCatalogDiagnostics getStyleCatalogDiagnostics();
YA_GUI_API void diagnoseStyleKey(std::string_view key, type_index_t styleType);

template <typename TStyle>
[[nodiscard]] inline EStyleKeyLookup lookupStyleKey(std::string_view key)
{
    return lookupStyleKey(key, ya::type_index_v<TStyle>);
}

/// Declared invalidation metadata for one reflected TStyle field.
/// `bPaint` is always set: a style write always needs a repaint.
/// `bLayout` is additional (fontSize/padding/minSize). Scrollbar `width` is
/// overlay chrome, not layout. `bResource` marks FBrush fields for later
/// async-ready invalidation; `invalidateProperty` has no Resource case yet.
struct FStyleFieldImpact
{
    bool bPaint    = true;
    bool bLayout   = false;
    bool bResource = false;

    bool operator==(const FStyleFieldImpact&) const = default;
};

[[nodiscard]] YA_GUI_API FStyleFieldImpact lookupStyleFieldImpact(type_index_t      styleType,
                                                                  std::string_view field);
[[nodiscard]] YA_GUI_API FStyleFieldImpact lookupStylePatchImpact(type_index_t         styleType,
                                                                  const nlohmann::json& patch);

template <typename TStyle>
[[nodiscard]] inline FStyleFieldImpact lookupStyleFieldImpact(std::string_view field)
{
    return lookupStyleFieldImpact(ya::type_index_v<TStyle>, field);
}

template <typename TStyle>
[[nodiscard]] inline FStyleFieldImpact lookupStylePatchImpact(const nlohmann::json& patch)
{
    return lookupStylePatchImpact(ya::type_index_v<TStyle>, patch);
}

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
        diagnoseStyleKey(name, ya::type_index_v<TStyle>);
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
