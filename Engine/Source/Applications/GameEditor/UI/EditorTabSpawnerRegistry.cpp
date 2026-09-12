#include "GameEditor/UI/EditorTabSpawnerRegistry.h"

#include "GameEditor/UI/EditorLevelEditorTab.h"
#include "GameEditor/UI/EditorAssetInspectorTab.h"
#include "GameEditor/UI/EditorContentBrowserTab.h"
#include "GameEditor/UI/EditorDebugImagesTab.h"
#include "GameEditor/UI/EditorFontAtlasTab.h"
#include "GameEditor/UI/EditorDocumentEditorTab.h"
#include "GameEditor/UI/EditorHierarchyTab.h"
#include "GameEditor/UI/EditorInspectorTab.h"
#include "GameEditor/UI/EditorPlayToolbarTab.h"
#include "GameEditor/UI/EditorRuntimeToolsTab.h"
#include "GameEditor/UI/EditorStatsTab.h"
#include "GameEditor/UI/EditorUIDesignerTab.h"
#include "GameEditor/UI/EditorUIDesignerTools.h"
#include "GameEditor/UI/EditorViewportTab.h"

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
        .tabId = "level-editor",
        .title = "Level",
        .toolsMenuLabel = "Level Editor",
        .scope = EEditorTabScope::WindowRootEditor,
        .ownerEditorId = kLevelEditorRootId,
        .placement = EEditorTabPlacement::WindowRootDock,
        .detachPolicy = EEditorTabDetachPolicy::Locked,
        .spawn = [](FEditorTabSpawnContext& ctx) -> std::shared_ptr<UIElement> {
            if (!ctx.nestedDock) {
                return nullptr;
            }
            return std::make_shared<EditorLevelEditorTab>(ctx.nestedDock);
        },
    });
    registry.add({
        .tabId = "viewport",
        .title = "Viewport",
        .toolsMenuLabel = "Viewport",
        .scope = EEditorTabScope::EditorOwnedTool,
        .ownerEditorId = kLevelEditorRootId,
        .placement = EEditorTabPlacement::EditorOwnedNested,
        .detachPolicy = EEditorTabDetachPolicy::Locked,
        .spawn = [](FEditorTabSpawnContext& ctx) -> std::shared_ptr<UIElement> {
            if (!ctx.viewportHost) {
                return nullptr;
            }
            return std::make_shared<EditorViewportTab>(ctx.viewportHost);
        },
    });
    registry.add({
        .tabId = "play-toolbar",
        .title = "Play",
        .toolsMenuLabel = "Play Toolbar",
        .scope = EEditorTabScope::EditorOwnedTool,
        .ownerEditorId = kLevelEditorRootId,
        .placement = EEditorTabPlacement::EditorOwnedNested,
        .detachPolicy = EEditorTabDetachPolicy::TearOffKeepOwner,
        .spawn = [](FEditorTabSpawnContext& ctx) -> std::shared_ptr<UIElement> {
            return std::make_shared<EditorPlayToolbarTab>(ctx.actions, ctx.app);
        },
    });
    registry.add({
        .tabId = "hierarchy",
        .title = "Hierarchy",
        .toolsMenuLabel = "Hierarchy",
        .scope = EEditorTabScope::EditorOwnedTool,
        .ownerEditorId = kLevelEditorRootId,
        .placement = EEditorTabPlacement::EditorOwnedNested,
        .detachPolicy = EEditorTabDetachPolicy::TearOffKeepOwner,
        .spawn = [](FEditorTabSpawnContext& ctx) -> std::shared_ptr<UIElement> {
            if (!ctx.layer || !ctx.selection || !ctx.actions) {
                return nullptr;
            }
            return std::make_shared<EditorHierarchyTab>(*ctx.layer, *ctx.selection, *ctx.actions);
        },
    });
    registry.add({
        .tabId = "inspector",
        .title = "Inspector",
        .toolsMenuLabel = "Inspector",
        .scope = EEditorTabScope::EditorOwnedTool,
        .ownerEditorId = kLevelEditorRootId,
        .placement = EEditorTabPlacement::EditorOwnedNested,
        .detachPolicy = EEditorTabDetachPolicy::TearOffKeepOwner,
        .spawn = [](FEditorTabSpawnContext& ctx) -> std::shared_ptr<UIElement> {
            if (!ctx.layer || !ctx.selection) {
                return nullptr;
            }
            return std::make_shared<EditorInspectorTab>(*ctx.layer, *ctx.selection, ctx.undo);
        },
    });
    registry.add({
        .tabId = "content-browser",
        .title = "Content",
        .toolsMenuLabel = "Content Browser",
        .scope = EEditorTabScope::WindowTool,
        .spawn = [](FEditorTabSpawnContext& ctx) -> std::shared_ptr<UIElement> {
            if (!ctx.layer) {
                return nullptr;
            }
            return std::make_shared<EditorContentBrowserTab>(*ctx.layer);
        },
    });
    registry.add({
        .tabId = "runtime-tools",
        .title = "Runtime",
        .toolsMenuLabel = "Runtime Tools",
        .scope = EEditorTabScope::WindowTool,
        .spawn = [](FEditorTabSpawnContext& ctx) {
            return std::make_shared<EditorRuntimeToolsTab>(ctx.actions, ctx.presentSurface, ctx.app);
        },
    });
    registry.add({
        .tabId = "ui-designer",
        .title = "UI",
        .toolsMenuLabel = "UI Designer",
        .scope = EEditorTabScope::WindowRootEditor,
        .ownerEditorId = kUIEditorRootId,
        .placement = EEditorTabPlacement::WindowRootDock,
        .detachPolicy = EEditorTabDetachPolicy::IndependentWindow,
        .spawn = [](FEditorTabSpawnContext& ctx) -> std::shared_ptr<UIElement> {
            if (!ctx.layer) {
                return nullptr;
            }
            return std::make_shared<EditorUIDesignerTab>(ctx);
        },
    });
    registry.add({
        .tabId = "ui-preview",
        .title = "UI Preview",
        .toolsMenuLabel = "UI Preview",
        .scope = EEditorTabScope::EditorOwnedTool,
        .ownerEditorId = kUIEditorRootId,
        .placement = EEditorTabPlacement::EditorOwnedNested,
        .detachPolicy = EEditorTabDetachPolicy::TearOffKeepOwner,
        .spawn = [](FEditorTabSpawnContext& ctx) -> std::shared_ptr<UIElement> {
            if (!ctx.layer) {
                return nullptr;
            }
            return std::make_shared<EditorUIPreviewTab>(*ctx.layer);
        },
    });
    registry.add({
        .tabId = "ui-hierarchy",
        .title = "UI Tree",
        .toolsMenuLabel = "UI Tree",
        .scope = EEditorTabScope::EditorOwnedTool,
        .ownerEditorId = kUIEditorRootId,
        .placement = EEditorTabPlacement::EditorOwnedNested,
        .detachPolicy = EEditorTabDetachPolicy::TearOffKeepOwner,
        .spawn = [](FEditorTabSpawnContext& ctx) -> std::shared_ptr<UIElement> {
            if (!ctx.layer) {
                return nullptr;
            }
            return std::make_shared<EditorUIHierarchyTab>(*ctx.layer);
        },
    });
    registry.add({
        .tabId = "ui-inspector",
        .title = "UI Inspector",
        .toolsMenuLabel = "UI Inspector",
        .scope = EEditorTabScope::EditorOwnedTool,
        .ownerEditorId = kUIEditorRootId,
        .placement = EEditorTabPlacement::EditorOwnedNested,
        .detachPolicy = EEditorTabDetachPolicy::TearOffKeepOwner,
        .spawn = [](FEditorTabSpawnContext& ctx) -> std::shared_ptr<UIElement> {
            if (!ctx.layer) {
                return nullptr;
            }
            return std::make_shared<EditorUIInspectorTab>(*ctx.layer, ctx.undo);
        },
    });
    registry.add({
        .tabId = "ui-parameters",
        .title = "Palette",
        .toolsMenuLabel = "UI Palette",
        .scope = EEditorTabScope::EditorOwnedTool,
        .ownerEditorId = kUIEditorRootId,
        .placement = EEditorTabPlacement::EditorOwnedNested,
        .detachPolicy = EEditorTabDetachPolicy::TearOffKeepOwner,
        .spawn = [](FEditorTabSpawnContext& ctx) -> std::shared_ptr<UIElement> {
            if (!ctx.layer) {
                return nullptr;
            }
            return std::make_shared<EditorUIPaletteTab>(*ctx.layer);
        },
    });
    registry.add({
        .tabId = "material-editor",
        .title = "Material",
        .toolsMenuLabel = "Material Editor",
        .scope = EEditorTabScope::WindowRootEditor,
        .ownerEditorId = kMaterialEditorRootId,
        .placement = EEditorTabPlacement::WindowRootDock,
        .detachPolicy = EEditorTabDetachPolicy::IndependentWindow,
        .spawn = [](FEditorTabSpawnContext& ctx) {
            return std::make_shared<EditorDocumentEditorTab>(kMaterialEditorRootId, ctx);
        },
    });
    registry.add({
        .tabId = "script-editor",
        .title = "Script",
        .toolsMenuLabel = "Script Editor",
        .scope = EEditorTabScope::WindowRootEditor,
        .ownerEditorId = kScriptEditorRootId,
        .placement = EEditorTabPlacement::WindowRootDock,
        .detachPolicy = EEditorTabDetachPolicy::IndependentWindow,
        .spawn = [](FEditorTabSpawnContext& ctx) {
            return std::make_shared<EditorDocumentEditorTab>(kScriptEditorRootId, ctx);
        },
    });

    const auto addDocumentTool = [&](const char* tabId,
                                     const char* title,
                                     EditorRootId rootId,
                                     EEditorDocumentKind kind,
                                     EEditorDocumentToolRole role) {
        registry.add({
            .tabId = tabId,
            .title = title,
            .toolsMenuLabel = title,
            .scope = EEditorTabScope::EditorOwnedTool,
            .ownerEditorId = rootId,
            .placement = EEditorTabPlacement::EditorOwnedNested,
            .detachPolicy = EEditorTabDetachPolicy::TearOffKeepOwner,
            .spawn = [kind, role](FEditorTabSpawnContext& ctx) {
                return std::make_shared<EditorDocumentToolTab>(kind, role, ctx);
            },
        });
    };
    addDocumentTool("material-preview", "Mat Preview", kMaterialEditorRootId,
                    EEditorDocumentKind::Material, EEditorDocumentToolRole::Preview);
    addDocumentTool("material-parameters", "Mat Params", kMaterialEditorRootId,
                    EEditorDocumentKind::Material, EEditorDocumentToolRole::Parameters);
    addDocumentTool("material-hierarchy", "Mat Hierarchy", kMaterialEditorRootId,
                    EEditorDocumentKind::Material, EEditorDocumentToolRole::Hierarchy);
    addDocumentTool("material-inspector", "Mat Inspector", kMaterialEditorRootId,
                    EEditorDocumentKind::Material, EEditorDocumentToolRole::Inspector);
    addDocumentTool("script-preview", "Script Preview", kScriptEditorRootId,
                    EEditorDocumentKind::Script, EEditorDocumentToolRole::Preview);
    addDocumentTool("script-parameters", "Script Params", kScriptEditorRootId,
                    EEditorDocumentKind::Script, EEditorDocumentToolRole::Parameters);
    addDocumentTool("script-hierarchy", "Script Hierarchy", kScriptEditorRootId,
                    EEditorDocumentKind::Script, EEditorDocumentToolRole::Hierarchy);
    addDocumentTool("script-inspector", "Script Inspector", kScriptEditorRootId,
                    EEditorDocumentKind::Script, EEditorDocumentToolRole::Inspector);
    registry.add({
        .tabId = "asset-inspector",
        .title = "Assets",
        .toolsMenuLabel = "Asset Inspector",
        .scope = EEditorTabScope::WindowTool,
        .spawn = [](FEditorTabSpawnContext& ctx) -> std::shared_ptr<UIElement> {
            if (!ctx.layer) {
                return nullptr;
            }
            return std::make_shared<EditorAssetInspectorTab>(*ctx.layer);
        },
    });
    registry.add({
        .tabId = "debug-images",
        .title = "Debug",
        .toolsMenuLabel = "Debug Images",
        .scope = EEditorTabScope::WindowTool,
        .spawn = [](FEditorTabSpawnContext& ctx) -> std::shared_ptr<UIElement> {
            if (!ctx.layer) {
                return nullptr;
            }
            return std::make_shared<EditorDebugImagesTab>(*ctx.layer);
        },
    });
    registry.add({
        .tabId = "font-atlases",
        .title = "Fonts",
        .toolsMenuLabel = "Font Atlases",
        .scope = EEditorTabScope::WindowTool,
        .spawn = [](FEditorTabSpawnContext&) -> std::shared_ptr<UIElement> {
            return std::make_shared<EditorFontAtlasTab>();
        },
    });
    registry.add({
        .tabId = "frame-stats",
        .title = "Stats",
        .toolsMenuLabel = "Frame Stats",
        .scope = EEditorTabScope::WindowTool,
        .spawn = [](FEditorTabSpawnContext& ctx) -> std::shared_ptr<UIElement> {
            if (!ctx.layer) {
                return nullptr;
            }
            return std::make_shared<EditorStatsTab>(*ctx.layer);
        },
    });
}

} // namespace ya
