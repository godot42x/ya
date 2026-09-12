#pragma once

// ============================================================================
// WorkbenchTheme - Tooling overlay on the shared chrome palette.
// Values live in GUI Runtime (`buildDefaultChromeTheme`). This header keeps
// the workbench entry name and token alias so Gallery / WorkbenchSurface
// do not change call sites.
// ============================================================================

#include "GUI/Widgets/DefaultChromeTheme.h"

namespace guiworkbench
{

namespace tokens = ya::gui_chrome::tokens;

inline std::shared_ptr<ya::UITheme> buildWorkbenchTheme(bool bDark)
{
    return ya::buildDefaultChromeTheme(bDark);
}

} // namespace guiworkbench
