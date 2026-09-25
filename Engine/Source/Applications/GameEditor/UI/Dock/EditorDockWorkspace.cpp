#include "GameEditor/UI/Dock/EditorDockWorkspace.h"

#include "Core/Config/ConfigManager.h"
#include "Core/Log.h"
#include "GUI/Widgets/Controls/DockSpace/DockContext.h"
#include "GUI/Widgets/Controls/DockSpace/DockNode.h"
#include "GUI/Widgets/Controls/DockSpace/DockSpace.h"
#include "GUI/Widgets/Controls/Menu.h"
#include "GUI/Widgets/Controls/MenuBar.h"
#include "GUI/Widgets/UIElement.h"
#include "GameEditor/UI/Dock/EditorNestedDockHost.h"
#include "GameEditor/UI/Dock/EditorWindowLayout.h"
#include "GameEditor/UI/Viewport/EditorViewportHost.h"

#include <cstddef>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_set>
#include <vector>

namespace ya
{

namespace
{

// Factory dock layout documents (first-run / Window>Reset / fallback). These
// literals are the single source of truth: nothing reads a layout JSON from
// disk at runtime, so edit here. v2 documents split the two concerns:
// `tree` is the node structure (splits + leaf references), `dockSpace` holds
// each leaf's stack data (role, panels, selection) keyed by leaf id.
//
// The window root is the *page well*: it lists the major editors and nothing
// else, so switching pages swaps the whole workspace. Each major editor's tool
// panels live in that editor's own nested dock (the per-root documents below),
// which is why Level's tools cannot be seen or docked while another editor's
// page is active.
constexpr std::string_view kFactoryWindowRootLayoutJson = R"JSON(
{
  "version": 2,
  "tree": { "kind": "leaf", "id": "page" },
  "dockSpace": {
    "page": {
      "role": "page",
      "hideTabBar": true,
      "panels": ["level-editor", "ui-designer"],
      "selected": "level-editor"
    }
  },
  "floating": []
}
)JSON";

constexpr std::string_view kFactoryOwnedNestedLayoutJson = R"JSON(
{
  "version": 2,
  "tree": {
    "kind": "split",
    "orientation": "horizontal",
    "ratio": 0.78,
    "minExtent": [120.0, 120.0],
    "children": [
      {
        "kind": "split",
        "orientation": "vertical",
        "ratio": 0.22,
        "minExtent": [120.0, 120.0],
        "children": [
          { "kind": "leaf", "id": "hierarchy" },
          {
            "kind": "split",
            "orientation": "vertical",
            "ratio": 0.0,
            "minExtent": [54.0, 80.0],
            "children": [
              { "kind": "leaf", "id": "play-toolbar" },
              { "kind": "leaf", "id": "viewport" }
            ]
          }
        ]
      },
      {
        "kind": "split",
        "orientation": "vertical",
        "ratio": 0.62,
        "minExtent": [120.0, 120.0],
        "children": [
          { "kind": "leaf", "id": "inspector" },
          { "kind": "leaf", "id": "tools" }
        ]
      }
    ]
  },
  "dockSpace": {
    "hierarchy":    { "panels": ["hierarchy"] },
    "play-toolbar": { "panels": ["play-toolbar"] },
    "viewport":     { "panels": ["viewport"] },
    "inspector":    { "panels": ["inspector"] },
    "tools": {
      "role": "tools",
      "panels": [
        "content-browser",
        "frame-stats",
        "runtime-tools",
        "render-settings",
        "asset-inspector",
        "debug-images",
        "font-atlases"
      ],
      "selected": "content-browser"
    }
  },
  "floating": []
}
)JSON";

constexpr std::string_view kFactoryUIOwnedNestedLayoutJson = R"JSON(
{
  "version": 2,
  "tree": {
    "kind": "split",
    "orientation": "vertical",
    "ratio": 0.72,
    "minExtent": [120.0, 120.0],
    "children": [
      {
        "kind": "split",
        "orientation": "horizontal",
        "ratio": 0.28,
        "minExtent": [80.0, 80.0],
        "children": [
          { "kind": "leaf", "id": "ui-parameters" },
          { "kind": "leaf", "id": "ui-preview" }
        ]
      },
      { "kind": "leaf", "id": "ui-inspector" }
    ]
  },
  "dockSpace": {
    "ui-parameters": { "panels": ["ui-parameters"] },
    "ui-preview":    { "panels": ["ui-preview", "ui-hierarchy"], "selected": "ui-preview" },
    "ui-inspector":  { "panels": ["ui-inspector"] }
  },
  "floating": []
}
)JSON";

constexpr std::string_view kFactoryMaterialOwnedNestedLayoutJson = R"JSON(
{
  "version": 2,
  "tree": {
    "kind": "split",
    "orientation": "vertical",
    "ratio": 0.72,
    "minExtent": [120.0, 120.0],
    "children": [
      {
        "kind": "split",
        "orientation": "horizontal",
        "ratio": 0.55,
        "minExtent": [80.0, 80.0],
        "children": [
          { "kind": "leaf", "id": "material-preview" },
          { "kind": "leaf", "id": "material-parameters" }
        ]
      },
      { "kind": "leaf", "id": "material-inspector" }
    ]
  },
  "dockSpace": {
    "material-preview":    { "panels": ["material-preview", "material-hierarchy"], "selected": "material-preview" },
    "material-parameters": { "panels": ["material-parameters"] },
    "material-inspector":  { "panels": ["material-inspector"] }
  },
  "floating": []
}
)JSON";

constexpr std::string_view kFactoryScriptOwnedNestedLayoutJson = R"JSON(
{
  "version": 2,
  "tree": {
    "kind": "split",
    "orientation": "vertical",
    "ratio": 0.72,
    "minExtent": [120.0, 120.0],
    "children": [
      {
        "kind": "split",
        "orientation": "horizontal",
        "ratio": 0.55,
        "minExtent": [80.0, 80.0],
        "children": [
          { "kind": "leaf", "id": "script-preview" },
          { "kind": "leaf", "id": "script-parameters" }
        ]
      },
      { "kind": "leaf", "id": "script-inspector" }
    ]
  },
  "dockSpace": {
    "script-preview":    { "panels": ["script-preview", "script-hierarchy"], "selected": "script-preview" },
    "script-parameters": { "panels": ["script-parameters"] },
    "script-inspector":  { "panels": ["script-inspector"] }
  },
  "floating": []
}
)JSON";

/// Pixel floor for a split edge. This is a min, not an allocation: do not
/// rewrite `ratio` (that steals divider drag and can pin pointer capture).
/// Updating the model is not enough: persist listeners do not rematerialize
/// DockSpace, so the live split mins are synced on the next arrange.
void applyHostedSplitMinExtent(FDockContext* dock, DockPanelId id, float extent)
{
    if (!dock || extent < 0.0f) {
        return;
    }
    FDockTreeModel& model = dock->dockModel();
    const FDockNode* leaf = model.findLeafForPanel(id);
    if (!leaf || !leaf->parent || leaf->parent->kind != EDockNodeKind::Split) {
        return;
    }
    FDockNode* split = leaf->parent;
    const int side = split->child[1].get() == leaf ? 1 : 0;
    split->minExtent[side] = extent;
    if (UIDockSpace* space = dock->dockSpace()) {
        space->markLayoutDirty();
    }
    dock->notifyDockLayoutListeners();
}

} // namespace

const nlohmann::json& EditorDockWorkspace::factoryLayout()
{
    static const nlohmann::json kLayout = nlohmann::json::parse(kFactoryWindowRootLayoutJson);
    return kLayout;
}

const nlohmann::json& EditorDockWorkspace::factoryOwnedNestedLayout()
{
    static const nlohmann::json kLayout = nlohmann::json::parse(kFactoryOwnedNestedLayoutJson);
    return kLayout;
}

const nlohmann::json& EditorDockWorkspace::factoryOwnedNestedLayoutFor(EditorRootId rootId)
{
    switch (rootId) {
    case kUIEditorRootId: {
        static const nlohmann::json kUI = nlohmann::json::parse(kFactoryUIOwnedNestedLayoutJson);
        return kUI;
    }
    case kMaterialEditorRootId: {
        static const nlohmann::json kMaterial = nlohmann::json::parse(kFactoryMaterialOwnedNestedLayoutJson);
        return kMaterial;
    }
    case kScriptEditorRootId: {
        static const nlohmann::json kScript = nlohmann::json::parse(kFactoryScriptOwnedNestedLayoutJson);
        return kScript;
    }
    default: {
        return factoryOwnedNestedLayout();
    }
    }
}

namespace
{

const nlohmann::json& factoryForPlacement(EEditorTabPlacement placement)
{
    return placement == EEditorTabPlacement::EditorOwnedNested
               ? EditorDockWorkspace::factoryOwnedNestedLayout()
               : EditorDockWorkspace::factoryLayout();
}

void forceCloseAllPanels(FDockContext& dock)
{
    for (const std::string& key : dock.panelStableKeys()) {
        (void)dock.setPanelClosable(key, true);
        (void)dock.closePanel(key);
    }
}

} // namespace

void EditorDockWorkspace::bind(FHost host)
{
    _host = std::move(host);
    applyAdoptPolicy();
}

void EditorDockWorkspace::applyAdoptPolicy()
{
    if (!_host.dock) {
        return;
    }
    const EEditorTabPlacement placement = _host.targetPlacement;
    const EditorRootId rootId = _host.activeRootId;
    const EditorTabSpawnerRegistry* spawners = _host.spawners;
    FDockContext* dock = _host.dock;
    _host.dock->canAdoptPanel = [placement, rootId, spawners](std::string_view stableKey,
                                                              uint32_t ownerEditorId,
                                                              std::string_view documentKey) {
        if (!spawners) {
            return true;
        }
        const FEditorTabSpawner* spawner = spawners->find(stableKey);
        if (!spawner) {
            return false;
        }
        FEditorTabDragPayload payload;
        payload.tabId = spawner->tabId;
        payload.scope = spawner->scope;
        payload.ownerEditorId =
            ownerEditorId != 0 ? static_cast<EditorRootId>(ownerEditorId) : spawner->ownerEditorId;
        payload.documentKey = std::string(documentKey);
        payload.detachPolicy = spawner->detachPolicy;
        payload.sourcePlacement = spawner->placement;
        if (spawner->scope == EEditorTabScope::WindowRootEditor && !canTearOffEditorTab(payload)) {
            return false;
        }
        return canAcceptEditorDrop(payload, placement, rootId);
    };
    if (placement != EEditorTabPlacement::WindowRootDock) {
        _host.dock->canAdoptOntoLeaf = nullptr;
        _host.dock->chooseAdoptLeaf = nullptr;
        return;
    }
    // The window root is the page well: it hosts major-editor pages and
    // nothing else. A tool panel is owned by one editor and lives in that
    // editor's own dock, so there is no window-level leaf for it to land in --
    // which is exactly what keeps Level's tools from appearing under the UI
    // editor's page.
    _host.dock->chooseAdoptLeaf = [dock](std::string_view,
                                         uint32_t,
                                         std::string_view) -> DockNodeId {
        if (!dock) {
            return kInvalidDockNodeId;
        }
        return dock->dockModel().findFirstLeafWithRole(EDockLeafRole::Page);
    };
    _host.dock->canAdoptOntoLeaf = [dock, spawners](std::string_view stableKey,
                                                    uint32_t,
                                                    std::string_view,
                                                    DockNodeId leafId,
                                                    bool bMerge) {
        if (!dock) {
            return true;
        }
        const FEditorTabSpawner* spawner = spawners ? spawners->find(stableKey) : nullptr;
        const FDockNode* leaf = dock->dockModel().findNode(leafId);
        if (!leaf || leaf->kind != EDockNodeKind::Stack) {
            return false;
        }
        // Only a page merges into the page well, and only on the center
        // target: a cardinal split would create a second page well, which the
        // chrome has no way to render.
        return spawner && spawner->scope == EEditorTabScope::WindowRootEditor && bMerge;
    };
}

nlohmann::json EditorDockWorkspace::layoutDocumentForPlacement(const nlohmann::json& document,
                                                               EEditorTabPlacement placement)
{
    // v5 envelope: OS windows[] (main record carries windowRoot / ownedNested
    // v2 dock documents). Dock-level native windows[] stays inside those
    // documents (MW-707). Anything older (v1-v4) or malformed falls back to
    // the factory layout.
    if (document.is_object() && document.value("version", 0) >= kEditorWindowLayoutVersion) {
        if (const nlohmann::json* record = findMainEditorWindowRecord(document)) {
            const char* field = placement == EEditorTabPlacement::EditorOwnedNested ? "ownedNested"
                                                                                    : "windowRoot";
            if (record->contains(field) && (*record)[field].is_object()) {
                return (*record)[field];
            }
        }
    }
    return factoryForPlacement(placement);
}

FEditorTabSpawnContext EditorDockWorkspace::makeSpawnContext(const FEditorTabSpawner& spawner) const
{
    FEditorTabSpawnContext ctx;
    ctx.windowId     = _host.windowId;
    ctx.scope        = spawner.scope;
    ctx.placement    = spawner.placement;
    ctx.detachPolicy = spawner.detachPolicy;
    ctx.tree         = _host.tree;
    ctx.layer        = _host.layer;
    ctx.selection    = _host.selection;
    ctx.actions      = _host.actions;
    ctx.undo         = _host.undo;
    ctx.viewportHost   = _host.viewportHost;
    ctx.app            = _host.app;
    ctx.presentSurface = _host.presentSurface;
    ctx.spawners       = _host.spawners;
    ctx.documents      = _host.documents;
    ctx.rootFor        = _host.rootFor;
    ctx.setMinExtent   = {};
    if (spawner.ownerEditorId != kInvalidEditorRootId) {
        ctx.ownerEditorId = spawner.ownerEditorId;
        if (_host.rootFor) {
            if (EditorRootSession* root = _host.rootFor(spawner.ownerEditorId)) {
                ctx.ownerRoot = root;
                ctx.selection = &root->selection();
                ctx.actions   = &root->actions();
                ctx.undo      = &root->undo();
            }
        }
    }
    ctx.documentKey = spawner.documentKey;
    if (ctx.documentKey.empty()) {
        const EditorRootId owner =
            spawner.ownerEditorId != kInvalidEditorRootId ? spawner.ownerEditorId : _host.activeRootId;
        if (ctx.ownerRoot && ctx.ownerRoot->document()) {
            ctx.documentKey = ctx.ownerRoot->document()->id().key;
        }
        else if (editorDocumentKindForRoot(owner) == EEditorDocumentKind::Scene) {
            // The window's documentKey is the scene. Only a Scene-kind root
            // may inherit it: a UI/Material/Script tool belongs to a document
            // of its own kind, and handing it the scene path would make the
            // panel claim a document it does not edit.
            ctx.documentKey = _host.documentKey;
        }
        else if (_host.rootFor) {
            if (EditorRootSession* root = _host.rootFor(owner)) {
                if (root->document()) {
                    ctx.documentKey = root->document()->id().key;
                }
            }
        }
    }
    // Level Editor reuses the window-owned nested dock. Other WindowRootEditors
    // create their own nested context inside EditorNestedDockHost.
    if ((spawner.scope == EEditorTabScope::WindowRootEditor ||
         spawner.scope == EEditorTabScope::EditorOwnedTool) &&
        spawner.ownerEditorId == _host.activeRootId) {
        ctx.nestedDock = _host.nestedDock;
    }
    return ctx;
}

void EditorDockWorkspace::buildWindowMenu()
{
    if (!_host.menuBar) {
        return;
    }
    _host.menuBar->addItem("Window", [this]() {
        std::vector<UIMenu::FItem> items;
        if (_host.spawners) {
            for (const FEditorTabSpawner& spawner : _host.spawners->all()) {
                const std::string tabId = spawner.tabId;
                const bool bLocked = spawner.detachPolicy == EEditorTabDetachPolicy::Locked;
                const bool bOpenHere = hasTab(tabId);
                EditorDockWorkspace* nested = nullptr;
                if (spawner.scope == EEditorTabScope::EditorOwnedTool &&
                    spawner.ownerEditorId != kInvalidEditorRootId) {
                    nested = nestedWorkspaceFor(spawner.ownerEditorId);
                }
                else {
                    nested = _host.nestedWorkspace;
                }
                const bool bOpenNested = nested && nested->hasTab(tabId);
                const bool bOpen = bOpenHere || bOpenNested;
                items.push_back({
                    .label    = spawner.title,
                    .action   = [this, tabId, bOpen, bLocked, bOpenHere, nested]() {
                        if (bOpen) {
                            if (bLocked) {
                                invokeTab(tabId);
                                return;
                            }
                            if (bOpenHere) {
                                (void)closeTab(tabId);
                            }
                            else if (nested) {
                                (void)nested->closeTab(tabId);
                            }
                            return;
                        }
                        invokeTab(tabId);
                    },
                    .bChecked = bOpen,
                });
            }
        }
        return UIMenu::create(std::move(items));
    });
    _host.menuBar->addItem("Layout", [this]() {
        return UIMenu::create({
            {
                .label  = "Default",
                .action = [this]() { resetLayout(); },
            },
        });
    });
}

bool EditorDockWorkspace::applyLayoutDocument(const nlohmann::json& layout, bool bFallbackToFactory)
{
    if (!_host.dock) {
        return false;
    }

    const auto applyOnce = [this](const nlohmann::json& document) -> bool {
        const std::vector<std::string> keys = FDockContext::collectLayoutPanelKeys(document);
        std::unordered_set<std::string> known;
        for (const std::string& id : keys) {
            if (materializeTab(id)) {
                known.insert(id);
            }
        }
        if (known.empty()) {
            return false;
        }
        nlohmann::json sanitized = FDockContext::sanitizeLayoutJson(document, known);
        if (FDockContext::collectLayoutPanelKeys(sanitized).empty()) {
            return false;
        }
        if (!_host.dock->importLayoutJson(sanitized)) {
            return false;
        }
        repairPlacement();
        // Spawn-complete mins run before import (root is still a single
        // stack). Re-apply after the split tree exists so Play hugs the
        // toolbar pixel min instead of the persisted stretch ratio.
        if (_host.spawners) {
            for (const std::string& key : _host.dock->panelStableKeys()) {
                const FEditorTabSpawner* spawner = _host.spawners->find(key);
                if (!spawner || !spawner->onSpawnComplete) {
                    continue;
                }
                const FDockContext::FPanel* panel = _host.dock->findPanelByStableKey(key);
                if (!panel || !panel->widget) {
                    continue;
                }
                FEditorTabSpawnContext ctx = makeSpawnContext(*spawner);
                ctx.setMinExtent = [dock = _host.dock, id = panel->id](float extent) {
                    applyHostedSplitMinExtent(dock, id, extent);
                };
                spawner->onSpawnComplete(ctx, *panel->widget);
            }
        }
        return true;
    };

    if (applyOnce(layout)) {
        return true;
    }
    if (!bFallbackToFactory) {
        return false;
    }

    YA_CORE_WARN("EditorDockWorkspace: layout document failed; applying factory layout");
    const nlohmann::json& factory = factoryForPlacement(_host.targetPlacement);
    const std::vector<std::string> factoryKeys = FDockContext::collectLayoutPanelKeys(factory);
    std::unordered_set<std::string> factorySet(factoryKeys.begin(), factoryKeys.end());
    for (const std::string& key : _host.dock->panelStableKeys()) {
        if (!factorySet.contains(key)) {
            (void)_host.dock->setPanelClosable(key, true);
            (void)_host.dock->closePanel(key);
        }
    }
    return applyOnce(factory);
}

void EditorDockWorkspace::repairPlacement()
{
    if (!_host.dock) {
        return;
    }
    FDockTreeModel& model = _host.dock->dockModel();
    model.pruneEmptyGenericLeaves();
    if (_host.targetPlacement != EEditorTabPlacement::WindowRootDock) {
        return;
    }
    // The page well is pages-only. A tool panel that a pre-ownership layout
    // parked here cannot be re-homed by this object (its owner's dock may not
    // exist yet); closing it is honest, and the Window menu reopens it in its
    // owner's dock. Leaving it would show one editor's tools under another
    // editor's page, which is the leak this policy removes.
    const DockNodeId page = model.findFirstLeafWithRole(EDockLeafRole::Page);
    const FDockNode* pageLeaf = model.findNode(page);
    if (!pageLeaf || pageLeaf->kind != EDockNodeKind::Stack) {
        return;
    }
    std::vector<DockPanelId> stray;
    stray.reserve(pageLeaf->panelIds.size());
    for (const DockPanelId id : pageLeaf->panelIds) {
        const FDockPanelRecord* record = model.findPanel(id);
        if (!record || !_host.spawners) {
            continue;
        }
        const FEditorTabSpawner* spawner = _host.spawners->find(record->stableKey);
        if (spawner && spawner->scope != EEditorTabScope::WindowRootEditor) {
            stray.push_back(id);
        }
    }
    if (stray.empty()) {
        return;
    }
    for (const DockPanelId id : stray) {
        if (const FDockPanelRecord* record = model.findPanel(id)) {
            (void)_host.dock->setPanelClosable(record->stableKey, true);
            (void)_host.dock->closePanel(record->stableKey);
        }
    }
}

void EditorDockWorkspace::applyWorkspaceLayout()
{
    nlohmann::json user;
    const nlohmann::json& factory = factoryForPlacement(_host.targetPlacement);
    if (ConfigManager::get().tryGet("editor", "dockLayout", user)) {
        (void)applyLayoutDocument(layoutDocumentForPlacement(user, _host.targetPlacement), true);
    }
    else {
        (void)applyLayoutDocument(factory, false);
    }
    if (_host.nestedWorkspace) {
        _host.nestedWorkspace->applyWorkspaceLayout();
    }
}

bool EditorDockWorkspace::materializeTab(std::string_view tabId)
{
    if (!_host.dock || tabId.empty()) {
        return false;
    }
    if (_host.dock->hasPanel(tabId)) {
        return true;
    }
    std::shared_ptr<UIElement> widget;
    std::string title{tabId};
    const FEditorTabSpawner* spawner = nullptr;
    uint32_t ownerEditorId = 0;
    std::string documentKey;
    if (_host.spawners) {
        spawner = _host.spawners->find(tabId);
        if (spawner) {
            // One predicate for spawn, restore, drop and redock: a saved
            // layout and a Window-menu click must agree on where a tab may
            // live, or a restore would materialize a panel the live policy
            // would refuse.
            if (!canSpawnEditorTab(spawner->ownership(),
                                   _host.targetPlacement,
                                   _host.activeRootId)) {
                YA_CORE_WARN("EditorDockWorkspace: tab '{}' cannot dock at placement {} under root {}",
                             tabId,
                             static_cast<int>(_host.targetPlacement),
                             _host.activeRootId);
                return false;
            }
            FEditorTabSpawnContext ctx = makeSpawnContext(*spawner);
            widget = spawner->spawn(ctx);
            title  = spawner->title;
            ownerEditorId = spawner->ownerEditorId;
            documentKey   = ctx.documentKey;
        }
    }
    if (!widget) {
        YA_CORE_WARN("EditorDockWorkspace: no spawner for tab '{}'", tabId);
        return false;
    }
    const DockPanelId id = _host.dock->addPanel(std::string(tabId), title, std::move(widget));
    if (id == kInvalidDockPanelId) {
        return false;
    }
    (void)_host.dock->setPanelIdentity(id, ownerEditorId, std::move(documentKey));
    if (spawner && spawner->detachPolicy == EEditorTabDetachPolicy::Locked) {
        (void)_host.dock->setPanelClosable(id, false);
    }
    repairPlacement();
    if (spawner && spawner->onSpawnComplete) {
        FEditorTabSpawnContext callbackContext = makeSpawnContext(*spawner);
        callbackContext.setMinExtent = [dock = _host.dock, id](float extent) {
            applyHostedSplitMinExtent(dock, id, extent);
        };
        if (const FDockContext::FPanel* panel = _host.dock->findPanel(id); panel && panel->widget) {
            spawner->onSpawnComplete(callbackContext, *panel->widget);
        }
    }
    return true;
}

EditorDockWorkspace* EditorDockWorkspace::nestedWorkspaceFor(EditorRootId rootId) const
{
    if (rootId == kLevelEditorRootId) {
        return _host.nestedWorkspace;
    }
    if (_host.targetPlacement == EEditorTabPlacement::EditorOwnedNested && _host.activeRootId == rootId) {
        return const_cast<EditorDockWorkspace*>(this);
    }
    const char* tabId = editorRootTabId(rootId);
    if (!tabId || !_host.dock) {
        return nullptr;
    }
    const FDockContext::FPanel* panel = _host.dock->findPanelByStableKey(tabId);
    if (!panel || !panel->widget) {
        return nullptr;
    }
    if (auto* host = dynamic_cast<EditorNestedDockHost*>(panel->widget.get())) {
        return &host->nestedWorkspace();
    }
    return nullptr;
}

EditorDockWorkspace* EditorDockWorkspace::workspaceHoldingTab(std::string_view tabId) const
{
    if (hasTab(tabId)) {
        return const_cast<EditorDockWorkspace*>(this);
    }
    const FEditorTabSpawner* spawner = _host.spawners ? _host.spawners->find(tabId) : nullptr;
    if (spawner && spawner->scope == EEditorTabScope::EditorOwnedTool &&
        spawner->ownerEditorId != kInvalidEditorRootId) {
        EditorDockWorkspace* nested = nestedWorkspaceFor(spawner->ownerEditorId);
        if (nested && nested != this && nested->hasTab(tabId)) {
            return nested;
        }
        return nullptr;
    }
    if (_host.nestedWorkspace && _host.nestedWorkspace != this &&
        _host.nestedWorkspace->hasTab(tabId)) {
        return _host.nestedWorkspace;
    }
    return nullptr;
}

bool EditorDockWorkspace::invokeTab(std::string_view tabId)
{
    if (EditorDockWorkspace* existing = workspaceHoldingTab(tabId)) {
        if (existing != this) {
            return existing->invokeTab(tabId);
        }
        if (!_host.dock) {
            return false;
        }
        repairPlacement();
        return _host.dock->activatePanel(tabId);
    }

    const FEditorTabSpawner* spawner = _host.spawners ? _host.spawners->find(tabId) : nullptr;
    if (spawner && spawner->scope == EEditorTabScope::EditorOwnedTool &&
        spawner->ownerEditorId != kInvalidEditorRootId &&
        spawner->ownerEditorId != _host.activeRootId &&
        _host.targetPlacement == EEditorTabPlacement::WindowRootDock) {
        const char* rootTab = editorRootTabId(spawner->ownerEditorId);
        if (!rootTab || !invokeTab(rootTab)) {
            return false;
        }
        if (EditorDockWorkspace* nested = nestedWorkspaceFor(spawner->ownerEditorId)) {
            return nested->invokeTab(tabId);
        }
        return false;
    }
    if (spawner && !canSpawnEditorTab(spawner->ownership(), _host.targetPlacement, _host.activeRootId)) {
        return _host.nestedWorkspace ? _host.nestedWorkspace->invokeTab(tabId) : false;
    }
    if (!_host.dock) {
        return false;
    }
    repairPlacement();
    if (!materializeTab(tabId)) {
        return false;
    }
    _host.dock->fireDockUpdated();
    return _host.dock->activatePanel(tabId);
}

bool EditorDockWorkspace::closeTab(std::string_view tabId)
{
    return _host.dock && _host.dock->closePanel(tabId);
}

bool EditorDockWorkspace::hasTab(std::string_view tabId) const
{
    return _host.dock && _host.dock->hasPanel(tabId);
}

void EditorDockWorkspace::resetLayout()
{
    if (!_host.dock) {
        return;
    }
    forceCloseAllPanels(*_host.dock);
    (void)applyLayoutDocument(factoryForPlacement(_host.targetPlacement), false);
    if (_host.nestedWorkspace) {
        _host.nestedWorkspace->resetLayout();
    }
    _host.dock->fireDockUpdated();
    persistLayout();
}

void EditorDockWorkspace::persistLayout()
{
    if (_host.persistAll) {
        _host.persistAll();
        return;
    }
    if (!_host.dock) {
        return;
    }
    ConfigManager::Editor("editor")
        .set("dockLayout", _host.dock->exportLayoutJson())
        .flush();
}

} // namespace ya
