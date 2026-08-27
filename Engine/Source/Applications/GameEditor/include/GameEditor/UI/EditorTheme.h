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

} // namespace ya
