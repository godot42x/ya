#include "GameEditor/UI/Dock/EditorNestedDockHost.h"

#include "GUI/Declarative/Build.h"
#include "GUI/Widgets/Controls/DockSpace/DockContext.h"
#include "GUI/Widgets/Controls/DockSpace/DockSpace.h"
#include "GameEditor/UI/Shell/EditorTabSpawnerRegistry.h"

namespace ya
{

EditorNestedDockHost::EditorNestedDockHost(const char* name, EditorRootId rootId, FEditorTabSpawnContext& ctx)
    : UICompoundWidget(name, "panel.canvas")
    , _rootId(rootId)
    , _nestedDock(ctx.nestedDock ? ctx.nestedDock : std::make_shared<FDockContext>())
{
    _nestedDock->bAllowFloating = false;
    _nestedDock->bAllowTearOff  = false;
    _nestedDock->sourceScope    = EDockSourceScope::EditorOwned;
    _nestedDock->hostWindowId   = ctx.windowId;
    _nested.bind({
        .tree            = ctx.tree,
        .layer           = ctx.layer,
        .selection       = ctx.selection,
        .actions         = ctx.actions,
        .undo            = ctx.undo,
        .spawners        = ctx.spawners,
        .documents       = ctx.documents,
        .dock            = _nestedDock.get(),
        .activeRootId    = rootId,
        .targetPlacement = EEditorTabPlacement::EditorOwnedNested,
        .windowId        = ctx.windowId,
        .documentKey     = ctx.documentKey,
        .app             = ctx.app,
        .presentSurface  = ctx.presentSurface,
        .rootFor         = ctx.rootFor,
    });
}

void EditorNestedDockHost::construct()
{
    if (!_nestedDock) {
        return;
    }
    addDetachedChild(ui::dockSpace(_name + "Dock").setContext(_nestedDock).release());
}

void EditorNestedDockHost::applyNestedFactoryLayout(const nlohmann::json& layout)
{
    (void)_nested.applyLayoutDocument(layout, false);
}

} // namespace ya
