#pragma once

#include "GUI/Host/GUIWindowSession.h"
#include "GUI/Widgets/Controls/DockSpace/DockContext.h"

namespace ya
{

/// Host adapter: bind a NativeWindow dock placement to a new extra session.
/// Window coordinator APIs never name dock types. Overlay placements return 0.
/// Does not migrate live widgets across trees (MW-703). The new window keeps
/// the standard OS chrome unless the caller opts into a mode explicitly.
[[nodiscard]] GUIWindowId realizeNativeDockPlacement(IGUIWindowCoordinator& coordinator,
                                                     FDockContext&          dock,
                                                     FDockFloatingWindowId  placementId,
                                                     IGUIAppDelegate&       content,
                                                     IRender*               render = nullptr,
                                                     EWindowChromeMode      chromeMode = EWindowChromeMode::Native);

} // namespace ya
