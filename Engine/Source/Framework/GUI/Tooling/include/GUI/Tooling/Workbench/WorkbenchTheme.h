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
// "panel.canvas") from the mounted WorkbenchTheme, so swapping the theme
// restyles the whole shell at once.
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
        dock.dropPreviewMergeColor = FBrush::Solid({0.28f, 0.52f, 0.90f, 0.08f});
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
        dock.dropPreviewMergeColor = FBrush::Solid({0.28f, 0.52f, 0.90f, 0.10f});
        theme->define<ya::FDockSpaceStyle>("dock", dock);

        auto floating = ya::FFloatingWindowStyle{};
        floating.bodyFill  = FBrush::Solid({0.93f, 0.94f, 0.96f, 0.985f});
        floating.innerFill = FBrush::Solid({0.86f, 0.87f, 0.90f, 0.55f});
        floating.borderColor    = {0.45f, 0.48f, 0.55f, 1.0f};
        floating.edgeAffordance = {0.50f, 0.56f, 0.70f, 0.42f};
        floating.titleTextColor = tokens::kTextColorLight;
        theme->define<ya::FFloatingWindowStyle>("floating", floating);
    }
    return theme;
}

} // namespace guiworkbench
