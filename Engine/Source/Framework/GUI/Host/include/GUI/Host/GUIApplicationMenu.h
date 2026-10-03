#pragma once

// ============================================================================
// macOS-only capability: the system application menu (NSApp main menu — the
// menu-bar row beside the notch on notched displays). Other platforms render
// menus inside the app window and have no "application menu" concept, so the
// whole API exists only on Apple builds; downstream keeps its in-window menu
// bar there.
// ============================================================================

#if defined(__APPLE__)

#include "Core/Api.h"

#include <functional>
#include <string>
#include <vector>

namespace ya
{

/// One entry of a native application menu. Descriptors are re-evaluated every
/// time their menu opens, so check/enabled states are always fresh.
struct YA_GUI_API FNativeMenuItem
{
    std::string           label;
    bool                  bSeparator = false;
    bool                  bCheckable = false;
    bool                  bChecked   = false;
    bool                  bEnabled   = true;
    std::function<void()> action;

    [[nodiscard]] static FNativeMenuItem separator()
    {
        FNativeMenuItem item;
        item.bSeparator = true;
        return item;
    }
};

/// One top-level menu of the native application menu bar. `buildItems` runs
/// every time the menu opens.
struct YA_GUI_API FNativeMenuDesc
{
    std::string                                   title;
    std::function<std::vector<FNativeMenuItem>()> buildItems;
};

/// Install NSApp's main menu. `buildMenus` runs on every menu open so entries
/// stay fresh; installing again replaces the menu. Returns false when the
/// provider is missing.
bool YA_GUI_API installApplicationMenu(std::function<std::vector<FNativeMenuDesc>()> buildMenus);

/// Re-run the provider over the top-level titles (call after adding menus to
/// the mirror so the bar picks them up without waiting for a menu open).
void YA_GUI_API refreshApplicationMenu();

/// Remove the installed native application menu (no-op when none installed).
void YA_GUI_API removeApplicationMenu();

} // namespace ya

#endif // __APPLE__
