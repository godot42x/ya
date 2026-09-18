#pragma once

#include "GUI/Host/GUIAppDelegate.h"
#include "GUI/Widgets/Controls/DockSpace/DockContext.h"
#include "GameEditor/UI/Shell/EditorWindowSession.h"

namespace ya
{

/// Delegate for an editor window whose host owns the tree but mounts no UI of
/// its own (torn-off windows before their content is rebuilt).
struct FEmptyGuiDelegate final : IGUIAppDelegate
{
    void buildUI(WidgetTree&) override {}
};

/// True when a dock projection still holds at least one panel.
[[nodiscard]] inline bool dockHasPanels(const FDockContext* dock)
{
    return dock && !dock->panelStableKeys().empty();
}

/// True when either dock projection of the session still holds a panel.
[[nodiscard]] inline bool sessionHasDockPanels(const EditorWindowSession& session)
{
    return dockHasPanels(session.surface().windowRootDock()) ||
           dockHasPanels(session.surface().ownedNestedDock());
}

} // namespace ya
