#include "GameEditor/UI/Dock/EditorNestedDockHost.h"

#include "GUI/Declarative/Build.h"
#include "GUI/Widgets/Controls/DockSpace/DockContext.h"
#include "GUI/Widgets/Controls/DockSpace/DockSpace.h"
#include "GameEditor/UI/Dock/EditorLayoutLibrary.h"
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

EditorNestedDockHost::~EditorNestedDockHost() = default;

void EditorNestedDockHost::construct()
{
    if (!_nestedDock) {
        return;
    }
    addDetachedChild(ui::dockSpace(_name + "Dock").setContext(_nestedDock).release());
}

void EditorNestedDockHost::applyNestedFactoryLayout(const nlohmann::json& layout)
{
    // The saved arrangement is the user's, so it wins over the shipped one. It
    // takes the same sanitize path as every other layout document, so a panel
    // whose spawner is gone is dropped rather than restored blindly.
    const std::string_view name = EditorDockWorkspace::nestedLayoutDocumentName(_rootId);
    const nlohmann::json   saved = EditorLayoutLibrary::get().document(name);
    (void)_nested.applyLayoutDocument(saved.is_object() && !saved.empty() ? saved : layout, false);

    // Subscribe only now: a page's tools live inside this widget, so the
    // arrangement dies with the tab unless it is written where the next
    // instance will read it -- but the apply above is not an arrangement.
    if (!_bPersistsArrangement) {
        _bPersistsArrangement = true;
        _nestedDock->appendOnDockUpdated([this]() { rememberLayout(); });
        _nestedDock->appendOnFloatingUpdated([this]() { rememberLayout(); });
    }
}

void EditorNestedDockHost::rememberLayout()
{
    if (!_nestedDock) {
        return;
    }
    const nlohmann::json arrangement = _nestedDock->exportLayoutJson();
    // A dock with no panels describes nothing anyone arranged. Writing it would
    // replace a good arrangement with an empty one, so the arrangement is only
    // recorded once there is something in it to record.
    if (FDockContext::collectLayoutPanelKeys(arrangement).empty()) {
        return;
    }
    (void)EditorLayoutLibrary::get().saveOverride(
        EditorDockWorkspace::nestedLayoutDocumentName(_rootId), arrangement);
}

} // namespace ya
