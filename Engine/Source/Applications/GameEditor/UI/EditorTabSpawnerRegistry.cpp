#include "GameEditor/UI/EditorTabSpawnerRegistry.h"

#include "GameEditor/UI/EditorAssetInspectorTab.h"
#include "GameEditor/UI/EditorContentBrowserTab.h"
#include "GameEditor/UI/EditorDebugImagesTab.h"
#include "GameEditor/UI/EditorHierarchyTab.h"
#include "GameEditor/UI/EditorInspectorTab.h"
#include "GameEditor/UI/EditorRuntimeToolsTab.h"
#include "GameEditor/UI/EditorStatsTab.h"
#include "GameEditor/UI/EditorUIDesignerTab.h"
#include "GameEditor/UI/EditorViewportTab.h"
#include "GameEditor/UI/EditorWorkbenchTab.h"
#include "GUI/Widgets/WidgetTree.h"

namespace ya
{

void EditorTabSpawnerRegistry::add(FEditorTabSpawner spawner)
{
    if (spawner.tabId.empty() || !spawner.spawn || find(spawner.tabId)) {
        return;
    }
    _spawners.push_back(std::move(spawner));
}

const FEditorTabSpawner* EditorTabSpawnerRegistry::find(std::string_view tabId) const
{
    for (const FEditorTabSpawner& spawner : _spawners) {
        if (spawner.tabId == tabId) {
            return &spawner;
        }
    }
    return nullptr;
}

void registerBuiltinEditorTabSpawners(EditorTabSpawnerRegistry& registry)
{
    registry.add({
        .tabId = "viewport",
        .title = "Viewport",
        .toolsMenuLabel = "Viewport",
        .spawn = [](FEditorTabSpawnContext& ctx) {
            return std::make_shared<EditorViewportTab>(ctx.viewportHost);
        },
    });
    registry.add({
        .tabId = "hierarchy",
        .title = "Hierarchy",
        .toolsMenuLabel = "Hierarchy",
        .spawn = [](FEditorTabSpawnContext& ctx) {
            return std::make_shared<EditorHierarchyTab>(ctx.layer, ctx.selection, ctx.actions);
        },
    });
    registry.add({
        .tabId = "inspector",
        .title = "Inspector",
        .toolsMenuLabel = "Inspector",
        .spawn = [](FEditorTabSpawnContext& ctx) {
            return std::make_shared<EditorInspectorTab>(ctx.layer, &ctx.undo);
        },
    });
    registry.add({
        .tabId = "content-browser",
        .title = "Content",
        .toolsMenuLabel = "Content Browser",
        .spawn = [](FEditorTabSpawnContext& ctx) {
            return std::make_shared<EditorContentBrowserTab>(ctx.layer);
        },
    });
    registry.add({
        .tabId = "runtime-tools",
        .title = "Runtime",
        .toolsMenuLabel = "Runtime Tools",
        .spawn = [](FEditorTabSpawnContext&) {
            return std::make_shared<EditorRuntimeToolsTab>();
        },
    });
    registry.add({
        .tabId = "ui-designer",
        .title = "UI",
        .toolsMenuLabel = "UI Designer",
        .spawn = [](FEditorTabSpawnContext& ctx) {
            return std::make_shared<EditorUIDesignerTab>(ctx.layer, &ctx.undo);
        },
    });
    registry.add({
        .tabId = "asset-inspector",
        .title = "Assets",
        .toolsMenuLabel = "Asset Inspector",
        .spawn = [](FEditorTabSpawnContext& ctx) {
            return std::make_shared<EditorAssetInspectorTab>(ctx.layer);
        },
    });
    registry.add({
        .tabId = "debug-images",
        .title = "Debug",
        .toolsMenuLabel = "Debug Images",
        .spawn = [](FEditorTabSpawnContext& ctx) {
            return std::make_shared<EditorDebugImagesTab>(ctx.layer);
        },
    });
    registry.add({
        .tabId = "frame-stats",
        .title = "Stats",
        .toolsMenuLabel = "Frame Stats",
        .spawn = [](FEditorTabSpawnContext& ctx) {
            return std::make_shared<EditorStatsTab>(ctx.layer);
        },
    });
    registry.add({
        .tabId = "gui-workbench",
        .title = "Workbench",
        .toolsMenuLabel = "GUI Workbench",
        .spawn = [](FEditorTabSpawnContext& ctx) {
            auto tab = std::make_shared<EditorWorkbenchTab>();
            if (ctx.authoringParent) {
                const WidgetAttachment attached = ctx.tree.attach(*ctx.authoringParent, tab);
                if (attached.valid()) {
                    tab->buildWorkbench(ctx.tree);
                    ctx.tree.detach(*tab);
                }
            }
            return tab;
        },
    });
}

} // namespace ya
