#pragma once

// ============================================================================
// DefaultChromeTheme - shared chrome palette for GUI apps (editor, workbench).
//
// Mechanism (UITheme / resolveThemeStyle / generation token) lives in the
// style system. This file owns the VALUES: one role palette + one set of typed
// styles baked from it, parameterised by look. Workbench and GameEditor both
// consume this builder so EditorTheme does not include Tooling.
//
// Structure: `FPalette` is the single source of truth. Every themed surface is
// a named ROLE (canvas < window < panel < raised, plus well/hover/pressed/
// selected, the text ramp, the border ramp and the status colours), and
// `palette(bDark)` returns the dark or light table. `defineChromeStyles` bakes
// the panel/frame keys AND the form-widget keys from that table in ONE pass.
// The previous builder restated every style once per look, which is how the
// light and dark looks drift apart; adding a key is now one role, not two
// blocks.
//
// Contrast contract (relative luminance; the dark palette is the tightest
// case): text >= 10:1 on every surface, text2 >= 5.9:1, text3 >= 3.6:1, i.e.
// the usual >= 4.5:1 body / >= 3:1 secondary split. Surfaces step by
// ~1.05-1.15:1 so adjacency stays readable without banding, and an edge
// carries >= 1.5:1 against the fill it outlines (a frame that does not
// separate is just a heavier fill).
//
// Radius lives in `radius`: roundness is part of the look's personality, not a
// per-style accident. Controls that derive roundness from their OWN extent (a
// switch knob, a radio dot) compute it themselves - the theme cannot know the
// widget's size.
// ============================================================================

#include "GUI/Widgets/Style.h"
#include "GUI/Widgets/Theme.h"

#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <string_view>

namespace ya::gui_chrome
{

// ============================================================================
// Design tokens: named raw palette material. The typed styles below are baked
// from these, never from literals.
// ============================================================================
namespace tokens
{

/// One role per visual decision, so a style says WHICH plane/state it means
/// instead of naming a colour - that is what lets one palette drive dark and
/// light without a second code path.
struct FPalette
{
    bool bDark = true;

    /// Surface ladder, deepest to highest: canvas -> window -> panel -> raised,
    /// with `well` for content that reads as sunk into its parent.
    glm::vec4 canvas = {0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4 window = {0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4 panel  = {0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4 raised = {0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4 well   = {0.0f, 0.0f, 0.0f, 1.0f};

    /// Interaction washes: one role per state, so a button, a row and a tab all
    /// light up by the same amount instead of each inventing its own delta.
    glm::vec4 hover    = {0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4 pressed  = {0.0f, 0.0f, 0.0f, 1.0f};
    /// Selection / current item: an accent TINT, not the accent itself (a full
    /// accent row would out-shout the primary action next to it).
    glm::vec4 selected = {0.0f, 0.0f, 0.0f, 1.0f};
    /// Primary action, focus, and "on" state.
    glm::vec4 accent = {0.0f, 0.0f, 0.0f, 1.0f};

    /// Text ramp: primary / secondary / tertiary. `disabled` sits deliberately
    /// below tertiary - it must read as unavailable, and it is never used for
    /// text that has to be read.
    glm::vec4 text     = {0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4 text2    = {0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4 text3    = {0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4 disabled = {0.0f, 0.0f, 0.0f, 1.0f};

    /// Edge ramp: `borderSubtle` separates sibling surfaces, `borderStrong`
    /// outlines a control that must look interactive, `borderHover` is the
    /// lifted edge of an input that is hovered or dragged.
    glm::vec4 borderSubtle = {0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4 borderStrong = {0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4 borderHover  = {0.0f, 0.0f, 0.0f, 1.0f};

    /// Status ink. Kept deliberately few: a status colour that is not one of
    /// these belongs to the app, not to the framework palette.
    glm::vec4 success = {0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4 error   = {0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4 warning = {0.0f, 0.0f, 0.0f, 1.0f};
};

/// Corner radius ramp (logical px). Smaller surfaces take smaller radii: one
/// radius everywhere is what makes themed chrome look pasted on.
namespace radius
{
inline constexpr float kChip    = 4.0f;  // badges, scrollbar thumb, check box
inline constexpr float kControl = 6.0f;  // button, field, combo, spin
inline constexpr float kTab     = 5.0f;  // tab strip button
inline constexpr float kRow     = 5.0f;  // list / tree / menu row
inline constexpr float kMenu    = 8.0f;  // popup, tooltip, floating window
inline constexpr float kCard    = 10.0f; // content card, dialog
} // namespace radius

/// Dark chrome: near-black surfaces with a cool cast, one bright accent, and
/// text held far above the noise floor of the surface ladder.
[[nodiscard]] inline constexpr FPalette darkPalette()
{
    FPalette p;
    p.canvas = {0.039f, 0.047f, 0.063f, 1.0f};
    p.window = {0.063f, 0.075f, 0.094f, 1.0f};
    p.panel  = {0.086f, 0.102f, 0.129f, 1.0f};
    p.raised = {0.110f, 0.129f, 0.165f, 1.0f};
    p.well   = {0.047f, 0.059f, 0.078f, 1.0f};

    p.hover    = {0.137f, 0.165f, 0.204f, 1.0f};
    p.pressed  = {0.169f, 0.200f, 0.247f, 1.0f};
    p.selected = {0.106f, 0.200f, 0.345f, 1.0f};
    p.accent   = {0.239f, 0.510f, 0.965f, 1.0f};

    p.text     = {0.910f, 0.918f, 0.941f, 1.0f};
    p.text2    = {0.663f, 0.698f, 0.753f, 1.0f};
    p.text3    = {0.498f, 0.541f, 0.600f, 1.0f};
    p.disabled = {0.357f, 0.392f, 0.447f, 1.0f};

    p.borderSubtle = {0.149f, 0.173f, 0.212f, 1.0f};
    p.borderStrong = {0.200f, 0.231f, 0.278f, 1.0f};
    p.borderHover  = {0.302f, 0.341f, 0.396f, 1.0f};

    p.success = {0.247f, 0.725f, 0.314f, 1.0f};
    p.error   = {0.941f, 0.322f, 0.322f, 1.0f};
    p.warning = {0.824f, 0.600f, 0.133f, 1.0f};
    return p;
}

/// Light chrome: white work surfaces over one grey canvas. On light, an edge
/// does the separating work that a dark surface ladder cannot.
[[nodiscard]] inline constexpr FPalette lightPalette()
{
    FPalette p;
    p.bDark  = false;
    p.canvas = {0.961f, 0.969f, 0.980f, 1.0f};
    p.window = {1.000f, 1.000f, 1.000f, 1.0f};
    p.panel  = {1.000f, 1.000f, 1.000f, 1.0f};
    p.raised = {0.976f, 0.980f, 0.988f, 1.0f};
    p.well   = {1.000f, 1.000f, 1.000f, 1.0f};

    p.hover    = {0.945f, 0.957f, 0.973f, 1.0f};
    p.pressed  = {0.906f, 0.925f, 0.949f, 1.0f};
    p.selected = {0.867f, 0.910f, 0.988f, 1.0f};
    p.accent   = {0.184f, 0.435f, 0.894f, 1.0f};

    p.text     = {0.082f, 0.094f, 0.114f, 1.0f};
    p.text2    = {0.325f, 0.353f, 0.396f, 1.0f};
    p.text3    = {0.447f, 0.478f, 0.525f, 1.0f};
    p.disabled = {0.639f, 0.667f, 0.706f, 1.0f};

    p.borderSubtle = {0.878f, 0.898f, 0.925f, 1.0f};
    p.borderStrong = {0.796f, 0.824f, 0.859f, 1.0f};
    p.borderHover  = {0.643f, 0.678f, 0.729f, 1.0f};

    p.success = {0.102f, 0.498f, 0.216f, 1.0f};
    p.error   = {0.776f, 0.157f, 0.157f, 1.0f};
    p.warning = {0.604f, 0.404f, 0.000f, 1.0f};
    return p;
}

[[nodiscard]] inline constexpr FPalette palette(bool bDark)
{
    return bDark ? darkPalette() : lightPalette();
}

/// A themed surface: fill + radius + edge as ONE value, which is exactly what
/// `FBrush` carries (see FBrush::cornerRadius / FBrush::borderColor). `border`
/// defaults to transparent, i.e. a plain fill.
[[nodiscard]] inline FBrush surface(const glm::vec4& fill,
                                    float            radius          = 0.0f,
                                    const glm::vec4& border          = {0.0f, 0.0f, 0.0f, 0.0f},
                                    float            borderThickness = 1.0f)
{
    return FBrush::solid(fill, radius, border, borderThickness);
}

/// Transparent fill: "no surface", used for states that only wash or outline.
inline const glm::vec4 kNoFill{0.0f, 0.0f, 0.0f, 0.0f};

// Legacy role aliases. Shell/gallery code that predates the role palette names
// these directly; they resolve INTO the palette, so there is still one source
// of truth (the values used to be restated per file).
inline constexpr glm::vec4 kWindowColor = darkPalette().window;
inline constexpr glm::vec4 kPanelColor  = darkPalette().panel;
inline constexpr glm::vec4 kCanvasColor = darkPalette().canvas;
inline constexpr glm::vec4 kHeaderColor = darkPalette().text2;
inline constexpr glm::vec4 kTextColor   = darkPalette().text;

} // namespace tokens

/// Bake every chrome style - frame/panel keys AND form-widget keys - from ONE
/// palette. `bDark` selects the table; it no longer selects a code path.
inline void defineChromeStyles(ya::UITheme& theme, const tokens::FPalette& p)
{
    using namespace tokens;
    namespace StyleKey = ya::StyleKey;

    const glm::vec4 selectionWash = {p.accent.r, p.accent.g, p.accent.b, 0.35f};

    const auto definePanel = [&theme](std::string_view key, const FBrush& fill) {
        ya::FPanelStyle style;
        style.fillColor = fill;
        theme.define<ya::FPanelStyle>(std::string(key), std::move(style));
    };

    const auto defineText = [&theme, &p](std::string_view key, const glm::vec4& color, uint32_t size) {
        ya::FTextStyle style;
        style.textColor = color;
        style.fontSize  = size;
        // Badge/chip fill for the opt-in `_bFillBackground` background.
        style.fillColor = surface(p.raised, radius::kChip, p.borderSubtle);
        style.padding   = {8.0f, 3.0f};
        theme.define<ya::FTextStyle>(std::string(key), std::move(style));
    };

    // === Surfaces ==========================================================
    definePanel(StyleKey::Panel, surface(p.panel));
    definePanel(StyleKey::PanelWindow, surface(p.window));
    definePanel(StyleKey::PanelTitlebar, surface(p.raised));
    definePanel(StyleKey::PanelCanvas, surface(p.canvas));
    definePanel(StyleKey::PanelSidebar, surface(p.window));
    definePanel(StyleKey::PanelSidebarCard, surface(p.panel, radius::kCard, p.borderSubtle));
    definePanel(StyleKey::PanelSurface, surface(p.panel, radius::kCard, p.borderSubtle));
    definePanel(StyleKey::MenuPanel, surface(p.raised, radius::kMenu, p.borderStrong));
    definePanel(StyleKey::Canvas, surface(p.canvas));
    definePanel(StyleKey::Tooltip, surface({p.raised.r, p.raised.g, p.raised.b, 0.98f},
                                           radius::kControl,
                                           p.borderStrong));
    definePanel(StyleKey::DragGhost, surface({p.accent.r, p.accent.g, p.accent.b, 0.90f}, radius::kControl));

    // === Text roles ========================================================
    defineText(StyleKey::Text, p.text, ya::gui_type::kBody);
    defineText(StyleKey::TextHeader, p.text, ya::gui_type::kHeader);
    defineText(StyleKey::TextSmall, p.text, ya::gui_type::kSmall);
    defineText(StyleKey::TextMuted, p.text2, ya::gui_type::kSmall);
    defineText(StyleKey::TextCaption, p.text3, ya::gui_type::kCaption);
    defineText(StyleKey::TextEyebrow, p.text3, ya::gui_type::kCaption);
    defineText(StyleKey::TextError, p.error, ya::gui_type::kSmall);

    // === Button ============================================================
    auto button = ya::FButtonStyle{};
    button.normalFill     = surface(p.raised, radius::kControl, p.borderStrong);
    button.hoveredFill    = surface(p.hover, radius::kControl, p.borderStrong);
    button.pressedFill    = surface(p.pressed, radius::kControl, p.borderStrong);
    button.focusedFill    = surface(p.selected, radius::kControl, p.accent);
    button.disabledFill   = surface(p.raised, radius::kControl, p.borderSubtle);
    button.selectedFill   = surface(p.accent, radius::kControl);
    button.errorFill      = surface(p.error, radius::kControl);
    button.dropTargetFill = surface(p.selected, radius::kControl, p.accent);
    button.textColor      = p.text;
    button.padding        = {12.0f, 5.0f};
    theme.define<ya::FButtonStyle>(std::string(StyleKey::Button), button);

    auto menubar = ya::FMenuBarItemStyle{};
    menubar.textColor = p.text2;
    // Flush with the titlebar until hovered: a permanently filled menu strip
    // reads as a second toolbar above the first one.
    menubar.normalFill     = surface(kNoFill, radius::kChip);
    menubar.hoveredFill    = surface(p.hover, radius::kChip);
    menubar.separatorColor = p.borderSubtle;
    theme.define<ya::FMenuBarItemStyle>(std::string(StyleKey::MenuBar), menubar);

    // === Tabs ==============================================================
    auto tab = ya::FTabStyle{};
    tab.textColor    = p.text2;
    tab.normalFill   = surface(kNoFill, radius::kTab);
    tab.hoveredFill  = surface(p.hover, radius::kTab);
    tab.selectedFill = surface(p.panel, radius::kTab, p.borderSubtle);
    tab.accentColor  = p.accent;
    tab.padding      = {9.0f, 3.0f};
    tab.separatorColor       = p.borderSubtle;
    tab.placeholderTextColor = p.text3;
    theme.define<ya::FTabStyle>(std::string(StyleKey::Tab), tab);

    auto sideTab         = tab;
    sideTab.selectedFill = surface(p.selected, radius::kRow, p.accent);
    sideTab.padding      = {10.0f, 4.0f};
    sideTab.separatorColor = p.borderSubtle;
    theme.define<ya::FTabStyle>(std::string(StyleKey::TabSidebar), sideTab);

    auto dockTab         = tab;
    dockTab.padding      = {7.0f, 3.0f};
    theme.define<ya::FTabStyle>(std::string(StyleKey::TabDock), dockTab);

    // === Splitters + scrollbars ============================================
    auto split = ya::FSplitPaneStyle{};
    // A hairline that becomes the accent when touched: enough to show the panes
    // are resizable, not a second border between two already-bordered panels.
    split.dividerFill         = surface(p.borderSubtle);
    split.dividerHoveredFill  = surface({p.accent.r, p.accent.g, p.accent.b, 0.55f});
    split.dividerDraggingFill = surface(p.accent);
    theme.define<ya::FSplitPaneStyle>(std::string(StyleKey::Split), split);

    auto scrollbar       = ya::FScrollBarStyle{};
    scrollbar.trackColor = surface(kNoFill);
    scrollbar.thumbColor = surface(p.disabled, radius::kChip);
    scrollbar.width      = 8.0f;
    theme.define<ya::FScrollBarStyle>(std::string(StyleKey::ScrollBar), scrollbar);

    auto dock = ya::FDockSpaceStyle{};
    dock.canvasColor             = surface(p.canvas);
    dock.dropPreviewColor        = surface({p.accent.r, p.accent.g, p.accent.b, 0.18f}, radius::kMenu);
    dock.dropPreviewMergeColor   = surface({p.success.r, p.success.g, p.success.b, 0.45f}, radius::kMenu);
    dock.dropPreviewOutlineColor = p.accent;
    theme.define<ya::FDockSpaceStyle>(std::string(StyleKey::Dock), dock);

    auto floating = ya::FFloatingWindowStyle{};
    floating.bodyFill       = surface({p.panel.r, p.panel.g, p.panel.b, 0.985f}, radius::kMenu, p.borderStrong);
    floating.innerFill      = surface(p.borderSubtle);
    floating.edgeAffordance = {p.text3.r, p.text3.g, p.text3.b, 0.42f};
    floating.titleTextColor = p.text;
    theme.define<ya::FFloatingWindowStyle>(std::string(StyleKey::Floating), floating);

    // === Containers / rows =================================================
    auto tree = ya::FTreeViewStyle{};
    tree.textColor        = p.text;
    tree.selectedFill     = surface(p.selected, radius::kRow, p.accent);
    tree.hoveredFill      = surface(p.hover, radius::kRow);
    tree.arrowColor       = p.text3;
    tree.arrowHoveredFill = surface(p.hover, radius::kChip);
    tree.dropIndicator    = p.accent;
    tree.fontSize         = ya::gui_type::kSmall;
    theme.define<ya::FTreeViewStyle>(std::string(StyleKey::Tree), tree);

    auto expander            = ya::FExpanderStyle{};
    expander.textColor       = p.text;
    expander.headerFill      = surface(kNoFill, radius::kRow);
    expander.hoveredFill     = surface(p.hover, radius::kRow);
    expander.pressedFill     = surface(p.pressed, radius::kRow);
    expander.focusedFill     = surface(p.selected, radius::kRow, p.accent);
    expander.arrowColor      = p.text3;
    expander.arrowHoveredFill = surface(p.hover, radius::kChip);
    expander.guideColor      = p.borderSubtle;
    expander.fontSize        = ya::gui_type::kSmall;
    theme.define<ya::FExpanderStyle>(std::string(StyleKey::Expander), expander);

    // Framed TreeNode look (`setFramed` / collapsingHeader): same type, with a
    // header that reads as its own bordered bar.
    auto expanderHeader         = expander;
    expanderHeader.headerFill   = surface(p.raised, radius::kControl, p.borderSubtle);
    expanderHeader.hoveredFill  = surface(p.hover, radius::kControl, p.borderSubtle);
    expanderHeader.pressedFill  = surface(p.pressed, radius::kControl, p.borderSubtle);
    expanderHeader.focusedFill  = surface(p.selected, radius::kControl, p.accent);
    expanderHeader.guideColor   = kNoFill;
    theme.define<ya::FExpanderStyle>(std::string(StyleKey::ExpanderHeader), expanderHeader);

    // === Inputs ===========================================================
    auto field = ya::FTextFieldStyle{};
    field.backgroundFill = surface(p.well, radius::kControl, p.borderStrong);
    field.hoveredFill    = surface(p.well, radius::kControl, p.borderHover);
    field.errorFill      = surface(p.well, radius::kControl, p.error);
    field.textColor      = p.text;
    field.caretColor     = p.accent;
    field.selectionColor = selectionWash;
    field.padding        = {6.0f, 3.0f};
    field.fontSize       = ya::gui_type::kBody;
    theme.define<ya::FTextFieldStyle>(std::string(StyleKey::TextField), field);

    auto fieldCompact     = field;
    fieldCompact.fontSize = ya::gui_type::kSmall;
    fieldCompact.padding  = {4.0f, 2.0f};
    theme.define<ya::FTextFieldStyle>(std::string(StyleKey::TextFieldCompact), fieldCompact);

    auto drag = ya::FDragFloatStyle{};
    drag.backgroundFill = surface(p.well, radius::kControl, p.borderStrong);
    drag.hoveredFill    = surface(p.well, radius::kControl, p.borderHover);
    drag.draggingFill   = surface(p.selected, radius::kControl, p.accent);
    drag.errorFill      = surface(p.well, radius::kControl, p.error);
    drag.textColor      = p.text;
    drag.padding        = {6.0f, 3.0f};
    drag.fontSize       = ya::gui_type::kSmall;
    theme.define<ya::FDragFloatStyle>(std::string(StyleKey::DragFloat), drag);

    auto spin = ya::FSpinBoxStyle{};
    spin.backgroundFill    = surface(p.well, radius::kControl, p.borderStrong);
    spin.hoveredFill       = surface(p.well, radius::kControl, p.borderHover);
    spin.buttonFill        = surface(p.raised, radius::kChip);
    spin.buttonHoveredFill = surface(p.hover, radius::kChip);
    spin.textColor         = p.text;
    spin.fontSize          = ya::gui_type::kSmall;
    theme.define<ya::FSpinBoxStyle>(std::string(StyleKey::SpinBox), spin);

    auto checkbox = ya::FCheckBoxStyle{};
    // Unchecked is a hollow ring, not a filled grey chip: the empty state has
    // to read as "nothing set yet", and only the checked state spends accent.
    checkbox.boxFill     = surface(p.well, radius::kChip, p.borderStrong);
    checkbox.hoveredFill = surface(p.hover, radius::kChip, p.borderStrong);
    checkbox.checkedFill = surface(p.accent, radius::kChip);
    checkbox.checkColor  = {0.980f, 0.988f, 1.000f, 1.0f};
    theme.define<ya::FCheckBoxStyle>(std::string(StyleKey::CheckBox), checkbox);

    // Switch: same family as the checkbox, so a switch inherits theme intent
    // instead of inventing a second palette.
    theme.define<ya::FCheckBoxStyle>(std::string(StyleKey::Switch), checkbox);

    auto combo = ya::FComboBoxStyle{};
    combo.fieldFill   = surface(p.well, radius::kControl, p.borderStrong);
    combo.hoveredFill = surface(p.well, radius::kControl, p.borderHover);
    combo.textColor   = p.text;
    combo.arrowColor  = p.text2;
    combo.fontSize    = ya::gui_type::kSmall;
    theme.define<ya::FComboBoxStyle>(std::string(StyleKey::ComboBox), combo);

    auto search = ya::FSearchComboStyle{};
    search.backgroundFill = surface(p.well, radius::kControl, p.borderStrong);
    search.hoveredFill    = surface(p.well, radius::kControl, p.borderHover);
    search.textColor      = p.text;
    search.caretColor     = p.accent;
    search.fontSize       = ya::gui_type::kSmall;
    theme.define<ya::FSearchComboStyle>(std::string(StyleKey::SearchCombo), search);

    auto slider = ya::FSliderStyle{};
    slider.trackFill = surface(p.borderStrong, radius::kChip);
    slider.valueFill = surface(p.accent, radius::kChip);
    slider.thumbFill = surface(p.text, radius::kChip);
    theme.define<ya::FSliderStyle>(std::string(StyleKey::Slider), slider);

    auto radio = ya::FRadioButtonStyle{};
    radio.hoveredFill  = surface(p.hover, radius::kRow);
    radio.dotColor     = surface(p.well, 0.0f, p.borderStrong);
    radio.dotFillColor = p.accent;
    radio.textColor    = p.text;
    radio.fontSize     = ya::gui_type::kSmall;
    theme.define<ya::FRadioButtonStyle>(std::string(StyleKey::Radio), radio);

    auto colorEdit = ya::FColorEditStyle{};
    colorEdit.backgroundFill = surface(p.well, radius::kControl, p.borderStrong);
    colorEdit.textColor      = p.text;
    colorEdit.padding        = {6.0f, 3.0f};
    colorEdit.fontSize       = ya::gui_type::kSmall;
    theme.define<ya::FColorEditStyle>(std::string(StyleKey::ColorEdit), colorEdit);

    auto table = ya::FTableGridStyle{};
    table.backgroundFill  = surface(p.well);
    table.selectedFill    = surface(p.selected);
    table.hoveredFill     = surface(p.hover);
    table.textColor       = p.text;
    table.headerTextColor = p.text3;
    table.gridColor       = p.borderSubtle;
    table.fontSize        = ya::gui_type::kSmall;
    theme.define<ya::FTableGridStyle>(std::string(StyleKey::Table), table);

    // === Menus / rows ======================================================
    auto menu = ya::FMenuStyle{};
    menu.itemNormalFill      = surface(kNoFill, radius::kRow);
    menu.itemHoveredFill     = surface(p.selected, radius::kRow);
    menu.textColor           = p.text;
    menu.iconColor           = p.text2;
    menu.checkmarkColor      = p.accent;
    menu.checkBoxBorderColor = p.text3;
    menu.shortcutColor       = p.text3;
    menu.disabledTextColor   = p.disabled;
    menu.disabledIconColor   = p.disabled;
    menu.separatorColor      = p.borderSubtle;
    menu.submenuArrowColor   = p.text3;
    menu.fontSize            = ya::gui_type::kSmall;
    theme.define<ya::FMenuStyle>(std::string(StyleKey::Menu), menu);

    auto selectable = ya::FSelectableRowStyle{};
    selectable.normalFill          = surface(kNoFill, radius::kRow);
    selectable.hoveredFill         = surface(p.hover, radius::kRow);
    selectable.selectedFill        = surface(p.selected, radius::kRow, p.accent);
    selectable.selectedHoveredFill = surface(p.selected, radius::kRow, p.accent);
    selectable.dropTargetFill      = surface(p.selected, radius::kRow, p.accent);
    selectable.errorFill           = surface(p.error, radius::kRow);
    selectable.disabledFill        = surface(kNoFill, radius::kRow);
    theme.define<ya::FSelectableRowStyle>(std::string(StyleKey::Selectable), selectable);

    auto image = ya::FImageStyle{};
    image.placeholderFill = surface(p.raised, radius::kChip, p.borderSubtle);
    theme.define<ya::FImageStyle>(std::string(StyleKey::Image), image);

    // No palette role yet: the popup host is a placement container, not a
    // surface (the popup's own panel key paints it).
    theme.define<ya::FPopupStyle>(std::string(StyleKey::Popup), ya::FPopupStyle{});

    auto dragSource = ya::FDragDropStyle{};
    dragSource.normalFill = surface(p.raised, radius::kControl, p.borderStrong);
    dragSource.activeFill = surface(p.selected, radius::kControl, p.accent);
    dragSource.textColor  = p.text;
    theme.define<ya::FDragDropStyle>(std::string(StyleKey::DragSource), dragSource);

    auto dragTarget = ya::FDragDropStyle{};
    dragTarget.normalFill = surface(p.well, radius::kControl, p.borderStrong);
    dragTarget.activeFill = surface({p.selected.r, p.selected.g, p.selected.b, 0.85f},
                                    radius::kControl,
                                    p.accent);
    dragTarget.textColor  = p.text;
    theme.define<ya::FDragDropStyle>(std::string(StyleKey::DragTarget), dragTarget);
}

/// Build the tree-level UITheme for a look (`bDark`). The theme defines every
/// canonical typed-style key the framework controls resolve, so mounting it
/// themes the whole workbench shell + demo pages.
inline std::shared_ptr<ya::UITheme> buildTheme(bool bDark)
{
    auto theme = std::make_shared<ya::UITheme>();
    defineChromeStyles(*theme, tokens::palette(bDark));
    return theme;
}

} // namespace ya::gui_chrome

namespace ya
{

inline std::shared_ptr<UITheme> buildDefaultChromeTheme(bool bDark)
{
    return gui_chrome::buildTheme(bDark);
}

} // namespace ya
