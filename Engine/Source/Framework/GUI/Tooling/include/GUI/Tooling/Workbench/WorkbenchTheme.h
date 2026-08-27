#pragma once

// ============================================================================
// WorkbenchTheme - the app/tooling THEME CONTENT for the style system
// (style-system Phase 4). The framework owns the resolve + invalidation
// mechanism (UITheme / resolveThemeStyle / generation token); this file owns
// the VALUES: design tokens (raw palette) baked into typed styles at theme
// construction (plan §3.4 config-time bake — no runtime token evaluation).
//
// The shell (FWorkbenchSurface) and the demo pages resolve every canonical
// typed-style key ("button"/"panel"/"menubar"/"tab"/"split"/"scrollbar"/
// "dock"/"floating"/"text") plus per-role shell keys ("panel.window",
// "panel.canvas", "text.header", "text.muted") and form-widget keys
// ("tree"/"textfield"/"menu"/"selectable"/"dragfloat"/"checkbox"/"combobox"/
// "slider"/"table"/"spinbox"/"radio"/"coloredit"/"searchcombo") from the mounted
// WorkbenchTheme, so swapping the theme restyles the whole shell at once.
// ============================================================================

#include "GUI/Widgets/Style.h"
#include "GUI/Widgets/Theme.h"

#include <memory>

namespace guiworkbench
{

/// Design tokens: named raw palette material. Typed styles below are baked
/// from these (still the hardened workbench look for dark).
namespace tokens
{
constexpr glm::vec4 kWindowColor  = {0.075f, 0.082f, 0.10f, 1.0f};
constexpr glm::vec4 kPanelColor   = {0.11f, 0.12f, 0.15f, 1.0f};
constexpr glm::vec4 kCanvasColor  = {0.05f, 0.055f, 0.07f, 1.0f};
constexpr glm::vec4 kHeaderColor  = {0.55f, 0.60f, 0.68f, 1.0f};
constexpr glm::vec4 kTextColor    = {0.88f, 0.90f, 0.94f, 1.0f};

// Dark button palette (current workbench look; theme-value source of the
// "button" key — buttons render through the theme, not these raw fields).
constexpr glm::vec4 kButtonNormal  = {0.16f, 0.18f, 0.22f, 1.0f};
constexpr glm::vec4 kButtonHovered = {0.24f, 0.28f, 0.34f, 1.0f};
constexpr glm::vec4 kButtonPressed = {0.10f, 0.11f, 0.14f, 1.0f};
constexpr glm::vec4 kButtonFocused = {0.26f, 0.52f, 0.90f, 1.0f};

// Light counterpart palette (Phase 4: a coherent light shell for the
// white/dark toggle).
constexpr glm::vec4 kWindowColorLight  = {0.86f, 0.87f, 0.89f, 1.0f};
constexpr glm::vec4 kPanelColorLight   = {0.93f, 0.94f, 0.96f, 1.0f};
constexpr glm::vec4 kCanvasColorLight  = {0.80f, 0.82f, 0.86f, 1.0f};
constexpr glm::vec4 kHeaderColorLight  = {0.18f, 0.20f, 0.26f, 1.0f};
constexpr glm::vec4 kTextColorLight    = {0.10f, 0.12f, 0.16f, 1.0f};
constexpr glm::vec4 kButtonNormalLight  = {0.94f, 0.95f, 0.97f, 1.0f};
constexpr glm::vec4 kButtonHoveredLight = {0.84f, 0.86f, 0.90f, 1.0f};
constexpr glm::vec4 kButtonPressedLight = {0.72f, 0.74f, 0.80f, 1.0f};
constexpr glm::vec4 kButtonFocusedLight = {0.55f, 0.75f, 0.95f, 1.0f};
} // namespace tokens

/// Form widgets + text roles used by chrome (tree, fields, menus, hierarchy
/// labels). Baked once so the light look does not fall back to dark defaults.
inline void defineWorkbenchContentStyles(ya::UITheme& theme, bool bDark)
{
    using ya::FBrush;
    const glm::vec4 text     = bDark ? tokens::kTextColor : tokens::kTextColorLight;
    const glm::vec4 header   = bDark ? tokens::kHeaderColor : tokens::kHeaderColorLight;
    const glm::vec4 muted    = bDark ? glm::vec4{0.70f, 0.74f, 0.80f, 1.0f}
                                     : glm::vec4{0.38f, 0.42f, 0.48f, 1.0f};
    const glm::vec4 error    = bDark ? glm::vec4{1.0f, 0.45f, 0.35f, 1.0f}
                                     : glm::vec4{0.78f, 0.18f, 0.12f, 1.0f};
    const glm::vec4 selected = bDark ? glm::vec4{0.22f, 0.42f, 0.78f, 1.0f}
                                     : glm::vec4{0.32f, 0.55f, 0.90f, 1.0f};
    const glm::vec4 hovered  = bDark ? glm::vec4{0.24f, 0.26f, 0.31f, 1.0f}
                                     : glm::vec4{0.84f, 0.86f, 0.90f, 1.0f};
    const glm::vec4 fieldBg  = bDark ? glm::vec4{0.08f, 0.09f, 0.12f, 1.0f}
                                     : glm::vec4{0.98f, 0.99f, 1.00f, 1.0f};
    const glm::vec4 menuItem = bDark ? glm::vec4{0.13f, 0.14f, 0.17f, 1.0f}
                                     : glm::vec4{0.93f, 0.94f, 0.96f, 1.0f};

    auto headerStyle = ya::FTextStyle{};
    headerStyle.textColor = bDark ? glm::vec4{0.90f, 0.92f, 0.95f, 1.0f} : text;
    headerStyle.fontSize  = 28;
    theme.define<ya::FTextStyle>("text.header", headerStyle);

    auto mutedStyle = ya::FTextStyle{};
    mutedStyle.textColor = muted;
    mutedStyle.fontSize  = 14;
    theme.define<ya::FTextStyle>("text.muted", mutedStyle);

    auto errorStyle = ya::FTextStyle{};
    errorStyle.textColor = error;
    errorStyle.fontSize  = 13;
    theme.define<ya::FTextStyle>("text.error", errorStyle);

    auto eyebrow = ya::FTextStyle{};
    eyebrow.textColor = header;
    eyebrow.fontSize  = 11;
    theme.define<ya::FTextStyle>("text.eyebrow", eyebrow);

    auto tree = ya::FTreeViewStyle{};
    tree.textColor        = text;
    tree.selectedFill     = FBrush::Solid(selected);
    tree.hoveredFill      = FBrush::Solid(hovered);
    tree.arrowColor       = muted;
    tree.arrowHoveredFill = FBrush::Solid(bDark ? glm::vec4{0.32f, 0.36f, 0.44f, 1.0f}
                                                : glm::vec4{0.78f, 0.80f, 0.85f, 1.0f});
    theme.define<ya::FTreeViewStyle>("tree", tree);

    auto field = ya::FTextFieldStyle{};
    field.backgroundFill = FBrush::Solid(fieldBg);
    field.textColor      = text;
    field.caretColor     = text;
    theme.define<ya::FTextFieldStyle>("textfield", field);

    auto menu = ya::FMenuStyle{};
    menu.itemNormalFill  = FBrush::Solid(menuItem);
    menu.itemHoveredFill = FBrush::Solid(selected);
    menu.textColor       = text;
    menu.iconColor       = muted;
    menu.checkmarkColor  = bDark ? glm::vec4{0.34f, 0.80f, 0.52f, 1.0f}
                                   : glm::vec4{0.18f, 0.60f, 0.34f, 1.0f};
    menu.shortcutColor   = muted;
    menu.disabledTextColor = bDark ? glm::vec4{0.42f, 0.46f, 0.54f, 1.0f}
                                      : glm::vec4{0.56f, 0.59f, 0.65f, 1.0f};
    menu.disabledIconColor = bDark ? glm::vec4{0.38f, 0.42f, 0.50f, 1.0f}
                                      : glm::vec4{0.60f, 0.63f, 0.69f, 1.0f};
    menu.separatorColor  = bDark ? glm::vec4{0.26f, 0.28f, 0.34f, 1.0f}
                                   : glm::vec4{0.76f, 0.78f, 0.82f, 1.0f};
    menu.submenuArrowColor = muted;
    theme.define<ya::FMenuStyle>("menu", menu);

    auto menuPanel = ya::FPanelStyle{};
    menuPanel.fillColor = FBrush::Solid(menuItem);
    theme.define<ya::FPanelStyle>("menu.panel", menuPanel);

    auto selectable = ya::FSelectableRowStyle{};
    selectable.hoveredFill         = FBrush::Solid(hovered);
    selectable.selectedFill        = FBrush::Solid(selected);
    selectable.selectedHoveredFill = FBrush::Solid(bDark ? glm::vec4{0.30f, 0.50f, 0.86f, 1.0f}
                                                         : glm::vec4{0.40f, 0.62f, 0.94f, 1.0f});
    theme.define<ya::FSelectableRowStyle>("selectable", selectable);

    auto drag = ya::FDragFloatStyle{};
    drag.backgroundFill = FBrush::Solid(bDark ? glm::vec4{0.17f, 0.19f, 0.24f, 1.0f}
                                              : glm::vec4{0.94f, 0.95f, 0.97f, 1.0f});
    drag.draggingFill   = FBrush::Solid(bDark ? glm::vec4{0.18f, 0.24f, 0.34f, 1.0f}
                                              : glm::vec4{0.84f, 0.88f, 0.95f, 1.0f});
    drag.textColor      = text;
    drag.borderColor    = bDark ? glm::vec4{0.30f, 0.33f, 0.40f, 1.0f}
                                : glm::vec4{0.70f, 0.72f, 0.76f, 1.0f};
    theme.define<ya::FDragFloatStyle>("dragfloat", drag);

    auto checkbox = ya::FCheckBoxStyle{};
    checkbox.boxFill     = FBrush::Solid(bDark ? glm::vec4{0.55f, 0.60f, 0.68f, 1.0f}
                                               : glm::vec4{0.70f, 0.73f, 0.78f, 1.0f});
    checkbox.hoveredFill = FBrush::Solid(hovered);
    checkbox.checkedFill = FBrush::Solid(selected);
    checkbox.checkColor  = bDark ? glm::vec4{0.95f, 0.96f, 0.98f, 1.0f} : glm::vec4{1.0f, 1.0f, 1.0f, 1.0f};
    theme.define<ya::FCheckBoxStyle>("checkbox", checkbox);

    auto combo = ya::FComboBoxStyle{};
    combo.fieldFill   = FBrush::Solid(fieldBg);
    combo.hoveredFill = FBrush::Solid(hovered);
    combo.textColor   = text;
    combo.arrowColor  = muted;
    theme.define<ya::FComboBoxStyle>("combobox", combo);

    auto slider = ya::FSliderStyle{};
    slider.trackFill = FBrush::Solid(bDark ? glm::vec4{0.14f, 0.16f, 0.20f, 1.0f}
                                           : glm::vec4{0.78f, 0.80f, 0.84f, 1.0f});
    slider.valueFill = FBrush::Solid(selected);
    slider.thumbFill = FBrush::Solid(bDark ? glm::vec4{0.88f, 0.90f, 0.94f, 1.0f} : text);
    theme.define<ya::FSliderStyle>("slider", slider);

    auto table = ya::FTableGridStyle{};
    table.backgroundFill  = FBrush::Solid(fieldBg);
    table.selectedFill    = FBrush::Solid(selected);
    table.hoveredFill     = FBrush::Solid(hovered);
    table.textColor       = text;
    table.headerTextColor = muted;
    table.gridColor       = bDark ? glm::vec4{0.20f, 0.22f, 0.27f, 1.0f}
                                  : glm::vec4{0.70f, 0.72f, 0.76f, 1.0f};
    theme.define<ya::FTableGridStyle>("table", table);

    auto spin = ya::FSpinBoxStyle{};
    spin.backgroundFill    = FBrush::Solid(bDark ? glm::vec4{0.17f, 0.19f, 0.24f, 1.0f}
                                                 : glm::vec4{0.94f, 0.95f, 0.97f, 1.0f});
    spin.buttonFill        = FBrush::Solid(bDark ? glm::vec4{0.22f, 0.24f, 0.30f, 1.0f}
                                                 : glm::vec4{0.86f, 0.88f, 0.92f, 1.0f});
    spin.buttonHoveredFill = FBrush::Solid(hovered);
    spin.textColor         = text;
    spin.borderColor       = bDark ? glm::vec4{0.30f, 0.33f, 0.40f, 1.0f}
                                   : glm::vec4{0.70f, 0.72f, 0.76f, 1.0f};
    theme.define<ya::FSpinBoxStyle>("spinbox", spin);

    auto radio = ya::FRadioButtonStyle{};
    radio.hoveredFill  = FBrush::Solid(hovered);
    radio.dotColor     = bDark ? glm::vec4{0.88f, 0.90f, 0.94f, 1.0f} : text;
    radio.dotFillColor = selected;
    radio.textColor    = text;
    theme.define<ya::FRadioButtonStyle>("radio", radio);

    auto colorEdit = ya::FColorEditStyle{};
    colorEdit.backgroundFill   = FBrush::Solid(fieldBg);
    colorEdit.textColor        = text;
    colorEdit.channelHighlight = selected;
    theme.define<ya::FColorEditStyle>("coloredit", colorEdit);

    auto search = ya::FSearchComboStyle{};
    search.backgroundFill = FBrush::Solid(fieldBg);
    search.hoveredFill    = FBrush::Solid(hovered);
    search.textColor      = text;
    search.caretColor     = text;
    theme.define<ya::FSearchComboStyle>("searchcombo", search);

    auto image = ya::FImageStyle{};
    image.placeholderFill = FBrush::Solid(bDark ? glm::vec4{0.24f, 0.26f, 0.31f, 1.0f}
                                                : glm::vec4{0.78f, 0.80f, 0.84f, 1.0f});
    theme.define<ya::FImageStyle>("image", image);

    auto popup = ya::FPopupStyle{};
    popup.modalFill = FBrush::Solid(bDark ? glm::vec4{0.0f, 0.0f, 0.0f, 0.45f}
                                          : glm::vec4{0.12f, 0.13f, 0.16f, 0.28f});
    theme.define<ya::FPopupStyle>("popup", popup);

    auto tooltip = ya::FPanelStyle{};
    tooltip.fillColor = FBrush::Solid(bDark ? glm::vec4{0.14f, 0.15f, 0.18f, 0.97f}
                                            : glm::vec4{0.98f, 0.98f, 0.99f, 0.97f});
    theme.define<ya::FPanelStyle>("tooltip", tooltip);

    auto ghost = ya::FPanelStyle{};
    ghost.fillColor = FBrush::Solid(bDark ? glm::vec4{0.24f, 0.46f, 0.82f, 0.75f}
                                          : glm::vec4{0.32f, 0.55f, 0.90f, 0.75f});
    theme.define<ya::FPanelStyle>("drag.ghost", ghost);
}

/// Build the tree-level UITheme for a look (`bDark`). The theme defines every
/// canonical typed-style key the framework controls resolve, so mounting it
/// themes the whole workbench shell + demo pages. Values are baked from the
/// token palette at construction.
inline std::shared_ptr<ya::UITheme> buildWorkbenchTheme(bool bDark)
{
    using ya::FBrush;

    auto theme = std::make_shared<ya::UITheme>();
    if (bDark) {
        const glm::vec4 window = tokens::kWindowColor;
        const glm::vec4 panel  = tokens::kPanelColor;

        auto button = ya::FButtonStyle{};
        button.normalFill  = FBrush::Solid(tokens::kButtonNormal);
        button.hoveredFill = FBrush::Solid(tokens::kButtonHovered);
        button.pressedFill = FBrush::Solid(tokens::kButtonPressed);
        button.focusedFill = FBrush::Solid(tokens::kButtonFocused);
        button.textColor   = tokens::kTextColor;
        theme->define<ya::FButtonStyle>("button", button);

        auto windowStyle = ya::FPanelStyle{};
        windowStyle.fillColor = FBrush::Solid(window);
        theme->define<ya::FPanelStyle>("panel.window", windowStyle);
        auto canvasStyle = ya::FPanelStyle{};
        canvasStyle.fillColor = FBrush::Solid(tokens::kCanvasColor);
        theme->define<ya::FPanelStyle>("panel.canvas", canvasStyle);
        auto panelStyle = ya::FPanelStyle{};
        panelStyle.fillColor = FBrush::Solid(panel);
        theme->define<ya::FPanelStyle>("panel", panelStyle);
        auto sidebarStyle = ya::FPanelStyle{};
        sidebarStyle.fillColor = FBrush::Solid({0.095f, 0.102f, 0.125f, 1.0f});
        theme->define<ya::FPanelStyle>("panel.sidebar", sidebarStyle);
        auto sidebarCardStyle = ya::FPanelStyle{};
        sidebarCardStyle.fillColor = FBrush::Solid({0.115f, 0.122f, 0.148f, 1.0f});
        theme->define<ya::FPanelStyle>("panel.sidebar.card", sidebarCardStyle);
        auto surfaceStyle = ya::FPanelStyle{};
        surfaceStyle.fillColor = FBrush::Solid({0.085f, 0.092f, 0.114f, 1.0f});
        theme->define<ya::FPanelStyle>("panel.surface", surfaceStyle);

        auto text = ya::FTextStyle{};
        text.textColor = tokens::kTextColor;
        text.fontSize  = 13;
        text.fillColor = FBrush::Solid({0.16f, 0.18f, 0.22f, 1.0f}); // badge fill
        text.padding   = {8.0f, 4.0f};
        theme->define<ya::FTextStyle>("text", text);

        auto menubar = ya::FMenuBarItemStyle{};
        // Lifted stops so hover is visible above the window background
        // (the old pre-theme workbench regression: default 0.10 normal sat
        // almost on the 0.075 backdrop; the shell previously re-colored the
        // items by hand — Phase 4 moves that INTO the theme).
        menubar.textColor   = tokens::kTextColor;
        menubar.normalFill  = FBrush::Solid({0.16f, 0.18f, 0.22f, 1.0f});
        menubar.hoveredFill = FBrush::Solid({0.30f, 0.33f, 0.40f, 1.0f});
        menubar.separatorColor = {0.24f, 0.26f, 0.32f, 1.0f};
        theme->define<ya::FMenuBarItemStyle>("menubar", menubar);

        auto tab = ya::FTabStyle{};
        tab.textColor    = tokens::kTextColor;
        tab.normalFill   = FBrush::Solid({0.15f, 0.16f, 0.19f, 1.0f});
        tab.hoveredFill  = FBrush::Solid({0.21f, 0.23f, 0.27f, 1.0f});
        tab.selectedFill = FBrush::Solid({0.12f, 0.13f, 0.17f, 1.0f});
        tab.accentColor  = {0.30f, 0.55f, 0.92f, 1.0f};
        tab.padding      = {14.0f, 6.0f};
        theme->define<ya::FTabStyle>("tab", tab);
        auto sideTab = tab;
        sideTab.normalFill = FBrush::Solid({0.12f, 0.13f, 0.16f, 0.0f});
        sideTab.hoveredFill = FBrush::Solid({0.17f, 0.19f, 0.24f, 1.0f});
        sideTab.selectedFill = FBrush::Solid({0.18f, 0.24f, 0.38f, 1.0f});
        sideTab.padding = {16.0f, 8.0f};
        sideTab.separatorColor = {0.20f, 0.22f, 0.28f, 1.0f};
        theme->define<ya::FTabStyle>("tab.sidebar", sideTab);
        auto dockTab = tab;
        dockTab.normalFill = FBrush::Solid({0.16f, 0.17f, 0.21f, 1.0f});
        dockTab.hoveredFill = FBrush::Solid({0.20f, 0.22f, 0.28f, 1.0f});
        dockTab.selectedFill = FBrush::Solid({0.18f, 0.20f, 0.25f, 1.0f});
        dockTab.padding = {14.0f, 8.0f};
        dockTab.separatorColor = {0.24f, 0.26f, 0.32f, 1.0f};
        theme->define<ya::FTabStyle>("tab.dock", dockTab);

        auto split = ya::FSplitPaneStyle{};
        split.dividerFill         = FBrush::Solid({0.11f, 0.12f, 0.15f, 1.0f});
        split.dividerHoveredFill  = FBrush::Solid({0.26f, 0.31f, 0.40f, 1.0f});
        split.dividerDraggingFill = FBrush::Solid({0.32f, 0.55f, 0.92f, 1.0f});
        theme->define<ya::FSplitPaneStyle>("split", split);

        auto scrollbar = ya::FScrollBarStyle{};
        scrollbar.trackColor = FBrush::Solid({0.10f, 0.11f, 0.14f, 0.9f});
        scrollbar.thumbColor = FBrush::Solid({0.34f, 0.38f, 0.46f, 1.0f});
        scrollbar.width      = 8.0f;
        theme->define<ya::FScrollBarStyle>("scrollbar", scrollbar);

        auto dock = ya::FDockSpaceStyle{};
        dock.canvasColor = FBrush::Solid({0.075f, 0.082f, 0.10f, 1.0f});
        dock.dropPreviewColor = FBrush::Solid({0.28f, 0.52f, 0.90f, 0.16f});
        dock.dropPreviewMergeColor = FBrush::Solid({0.26f, 0.76f, 0.46f, 0.45f});
        theme->define<ya::FDockSpaceStyle>("dock", dock);

        auto floating = ya::FFloatingWindowStyle{};
        floating.bodyFill  = FBrush::Solid({0.145f, 0.150f, 0.180f, 0.985f});
        floating.innerFill = FBrush::Solid({0.08f, 0.09f, 0.12f, 0.55f});
        floating.titleTextColor = tokens::kTextColor;
        theme->define<ya::FFloatingWindowStyle>("floating", floating);
    }
    else {
        const glm::vec4 window = tokens::kWindowColorLight;
        const glm::vec4 panel  = tokens::kPanelColorLight;

        auto button = ya::FButtonStyle{};
        button.normalFill  = FBrush::Solid(tokens::kButtonNormalLight);
        button.hoveredFill = FBrush::Solid(tokens::kButtonHoveredLight);
        button.pressedFill = FBrush::Solid(tokens::kButtonPressedLight);
        button.focusedFill = FBrush::Solid(tokens::kButtonFocusedLight);
        button.textColor   = tokens::kTextColorLight;
        theme->define<ya::FButtonStyle>("button", button);

        auto windowStyle = ya::FPanelStyle{};
        windowStyle.fillColor = FBrush::Solid(window);
        theme->define<ya::FPanelStyle>("panel.window", windowStyle);
        auto canvasStyle = ya::FPanelStyle{};
        canvasStyle.fillColor = FBrush::Solid(tokens::kCanvasColorLight);
        theme->define<ya::FPanelStyle>("panel.canvas", canvasStyle);
        auto panelStyle = ya::FPanelStyle{};
        panelStyle.fillColor = FBrush::Solid(panel);
        theme->define<ya::FPanelStyle>("panel", panelStyle);
        auto sidebarStyle = ya::FPanelStyle{};
        sidebarStyle.fillColor = FBrush::Solid({0.90f, 0.91f, 0.94f, 1.0f});
        theme->define<ya::FPanelStyle>("panel.sidebar", sidebarStyle);
        auto sidebarCardStyle = ya::FPanelStyle{};
        sidebarCardStyle.fillColor = FBrush::Solid({0.95f, 0.96f, 0.98f, 1.0f});
        theme->define<ya::FPanelStyle>("panel.sidebar.card", sidebarCardStyle);
        auto surfaceStyle = ya::FPanelStyle{};
        surfaceStyle.fillColor = FBrush::Solid({0.88f, 0.89f, 0.93f, 1.0f});
        theme->define<ya::FPanelStyle>("panel.surface", surfaceStyle);

        auto text = ya::FTextStyle{};
        text.textColor = tokens::kTextColorLight;
        text.fontSize  = 13;
        text.fillColor = FBrush::Solid({0.94f, 0.95f, 0.97f, 1.0f});
        text.padding   = {8.0f, 4.0f};
        theme->define<ya::FTextStyle>("text", text);

        auto menubar = ya::FMenuBarItemStyle{};
        menubar.textColor   = tokens::kTextColorLight;
        menubar.normalFill  = FBrush::Solid({0.84f, 0.86f, 0.89f, 1.0f});
        menubar.hoveredFill = FBrush::Solid({0.78f, 0.80f, 0.85f, 1.0f});
        menubar.separatorColor = {0.70f, 0.72f, 0.76f, 1.0f};
        theme->define<ya::FMenuBarItemStyle>("menubar", menubar);

        auto tab = ya::FTabStyle{};
        tab.textColor    = tokens::kTextColorLight;
        tab.normalFill   = FBrush::Solid({0.86f, 0.87f, 0.90f, 1.0f});
        tab.hoveredFill  = FBrush::Solid({0.80f, 0.82f, 0.86f, 1.0f});
        tab.selectedFill = FBrush::Solid({0.93f, 0.94f, 0.96f, 1.0f});
        tab.accentColor  = {0.30f, 0.55f, 0.92f, 1.0f};
        tab.padding      = {14.0f, 6.0f};
        tab.separatorColor      = {0.60f, 0.62f, 0.66f, 1.0f};
        tab.placeholderTextColor = {0.45f, 0.48f, 0.55f, 1.0f};
        theme->define<ya::FTabStyle>("tab", tab);
        auto sideTab = tab;
        sideTab.normalFill = FBrush::Solid({0.90f, 0.91f, 0.94f, 0.0f});
        sideTab.hoveredFill = FBrush::Solid({0.83f, 0.85f, 0.89f, 1.0f});
        sideTab.selectedFill = FBrush::Solid({0.76f, 0.84f, 0.95f, 1.0f});
        sideTab.padding = {16.0f, 8.0f};
        sideTab.separatorColor = {0.72f, 0.74f, 0.78f, 1.0f};
        theme->define<ya::FTabStyle>("tab.sidebar", sideTab);
        auto dockTab = tab;
        dockTab.normalFill = FBrush::Solid({0.90f, 0.91f, 0.94f, 1.0f});
        dockTab.hoveredFill = FBrush::Solid({0.84f, 0.86f, 0.90f, 1.0f});
        dockTab.selectedFill = FBrush::Solid({0.94f, 0.95f, 0.98f, 1.0f});
        dockTab.padding = {14.0f, 8.0f};
        dockTab.separatorColor = {0.70f, 0.72f, 0.76f, 1.0f};
        theme->define<ya::FTabStyle>("tab.dock", dockTab);

        auto split = ya::FSplitPaneStyle{};
        split.dividerFill         = FBrush::Solid({0.70f, 0.72f, 0.76f, 1.0f});
        split.dividerHoveredFill  = FBrush::Solid({0.55f, 0.60f, 0.70f, 1.0f});
        split.dividerDraggingFill = FBrush::Solid({0.32f, 0.55f, 0.92f, 1.0f});
        theme->define<ya::FSplitPaneStyle>("split", split);

        auto scrollbar = ya::FScrollBarStyle{};
        scrollbar.trackColor = FBrush::Solid({0.82f, 0.84f, 0.87f, 0.9f});
        scrollbar.thumbColor = FBrush::Solid({0.55f, 0.58f, 0.64f, 1.0f});
        scrollbar.width      = 8.0f;
        theme->define<ya::FScrollBarStyle>("scrollbar", scrollbar);

        auto dock = ya::FDockSpaceStyle{};
        dock.canvasColor = FBrush::Solid({0.75f, 0.77f, 0.81f, 1.0f});
        dock.dropPreviewColor = FBrush::Solid({0.28f, 0.52f, 0.90f, 0.18f});
        dock.dropPreviewMergeColor = FBrush::Solid({0.26f, 0.76f, 0.46f, 0.50f});
        theme->define<ya::FDockSpaceStyle>("dock", dock);

        auto floating = ya::FFloatingWindowStyle{};
        floating.bodyFill  = FBrush::Solid({0.93f, 0.94f, 0.96f, 0.985f});
        floating.innerFill = FBrush::Solid({0.86f, 0.87f, 0.90f, 0.55f});
        floating.borderColor    = {0.45f, 0.48f, 0.55f, 1.0f};
        floating.edgeAffordance = {0.50f, 0.56f, 0.70f, 0.42f};
        floating.titleTextColor = tokens::kTextColorLight;
        theme->define<ya::FFloatingWindowStyle>("floating", floating);
    }
    defineWorkbenchContentStyles(*theme, bDark);
    return theme;
}

} // namespace guiworkbench
