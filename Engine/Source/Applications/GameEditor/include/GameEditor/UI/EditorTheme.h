#pragma once

// ============================================================================
// EditorTheme - GameEditor chrome theme content.
//
// Mechanism (UITheme / resolveThemeStyle / generation token) lives in the
// GUI framework. This file owns the VALUES the editor chrome resolves.
// Family keys match the workbench shell (`panel.window`, `text.header`,
// `tree`, ...); `editor.*` is reserved for explicit overrides when the
// palettes diverge. Until they do, EditorTheme bakes the same tokens as
// WorkbenchTheme so GameEditor does not call the workbench builder directly.
// ============================================================================

#include "GUI/Tooling/Workbench/WorkbenchTheme.h"

namespace ya
{

inline std::shared_ptr<UITheme> buildEditorTheme(bool bDark)
{
    return guiworkbench::buildWorkbenchTheme(bDark);
}

/// Shared chrome metrics. Inspector rows, list rows, and the editor toolbar
/// read these instead of scattering 8/12/22/26 literals.
namespace editor_density
{
inline constexpr float kRowHeight         = 22.0f;
inline constexpr float kLabelColumn       = 140.0f;
inline constexpr float kRowSpacing        = 6.0f;
inline constexpr float kControlSpacing    = 6.0f;
inline constexpr float kPanelPadding      = 8.0f;
inline constexpr float kSectionSpacing    = 10.0f;
inline constexpr float kGroupHeaderHeight = 18.0f;
inline constexpr float kToolbarHeight     = 26.0f;
inline constexpr float kMenuHeight        = 24.0f;
inline constexpr float kListRowHeight     = 22.0f;
inline constexpr float kToolbarIconSize   = 16.0f;
inline constexpr float kListIconSize      = 16.0f;
}

/// Editor chrome icon assets (same files `EditorLayer::onAttach` loads).
namespace editor_icons
{
inline constexpr const char* kPlay     = "Engine/Content/TestTextures/editor/play.png";
inline constexpr const char* kStop     = "Engine/Content/TestTextures/editor/stop.png";
inline constexpr const char* kSimulate = "Engine/Content/TestTextures/editor/simulate_button.png";
inline constexpr const char* kFolder   = "Engine/Content/TestTextures/editor/folder2.png";
inline constexpr const char* kFile     = "Engine/Content/TestTextures/editor/file.png";
}

} // namespace ya
