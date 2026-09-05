#include "GameEditor/UI/EditorDockWorkspace.h"

#include "Core/Config/ConfigManager.h"
#include "Core/Log.h"
#include "GUI/Widgets/Controls/DockSpace/DockContext.h"
#include "GUI/Widgets/Controls/DockSpace/DockNode.h"
#include "GUI/Widgets/Controls/Menu.h"
#include "GUI/Widgets/Controls/MenuBar.h"
#include "GUI/Widgets/UIElement.h"
#include "GameEditor/UI/EditorViewportHost.h"

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace ya
{

namespace
{

DockPanelId dockPanelIdForKey(const FDockTreeModel& model, const char* stableKey)
{
    if (const FDockPanelRecord* record = model.findPanelByStableKey(stableKey)) {
        return record->id;
    }
    return kInvalidDockPanelId;
}

constexpr std::string_view kDefaultWorkspaceTabs[] = {
    "viewport",
    "hierarchy",
    "inspector",
    "content-browser",
    "frame-stats",
    "gui-workbench",
    "runtime-tools",
    "ui-designer",
    "asset-inspector",
    "debug-images",
};

} // namespace

FEditorTabSpawnContext EditorDockWorkspace::makeSpawnContext() const
{
    return FEditorTabSpawnContext{
        .tree            = *_host.tree,
        .layer           = *_host.layer,
        .selection       = *_host.selection,
        .actions         = *_host.actions,
        .undo            = *_host.undo,
        .authoringParent = _host.authoringParent,
        .viewportHost    = _host.viewportHost,
    };
}

void EditorDockWorkspace::buildToolsMenu()
{
    if (!_host.menuBar) {
        return;
    }
    _host.menuBar->addItem("Tools", [this]() {
        std::vector<UIMenu::FItem> items;
        if (_host.spawners) {
            for (const FEditorTabSpawner& spawner : _host.spawners->all()) {
                if (spawner.toolsMenuLabel.empty()) {
                    continue;
                }
                const std::string tabId = spawner.tabId;
                items.push_back({
                    .label  = spawner.toolsMenuLabel,
                    .action = [this, tabId]() { invokeTab(tabId); },
                });
            }
        }
        return UIMenu::create(std::move(items));
    });
}

void EditorDockWorkspace::materializeWorkspaceTabs()
{
    nlohmann::json layout;
    std::vector<std::string> keys;
    if (ConfigManager::get().tryGet("editor", "dockLayout", layout)) {
        keys = FDockContext::collectLayoutPanelKeys(layout);
    }
    if (keys.empty()) {
        for (std::string_view id : kDefaultWorkspaceTabs) {
            keys.emplace_back(id);
        }
    }
    for (const std::string& id : keys) {
        materializeTab(id);
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
    if (_host.spawners) {
        if (const FEditorTabSpawner* spawner = _host.spawners->find(tabId)) {
            FEditorTabSpawnContext ctx = makeSpawnContext();
            widget = spawner->spawn(ctx);
            title  = spawner->title;
        }
    }
    if (!widget) {
        YA_CORE_WARN("EditorSurface: no spawner for tab '{}'", tabId);
        return false;
    }
    return _host.dock->addPanel(std::string(tabId), title, std::move(widget)) != kInvalidDockPanelId;
}

bool EditorDockWorkspace::invokeTab(std::string_view tabId)
{
    if (!_host.dock) {
        return false;
    }
    if (_host.dock->hasPanel(tabId)) {
        return _host.dock->activatePanel(tabId);
    }
    if (!materializeTab(tabId)) {
        return false;
    }
    const FDockContext::FPanel* spawned = _host.dock->findPanelByStableKey(tabId);
    if (!spawned) {
        return false;
    }
    if (const FDockContext::FPanel* content = _host.dock->findPanelByStableKey("content-browser")) {
        if (FDockNode* leaf = _host.dock->dockModel().findLeafForPanel(content->id)) {
            (void)_host.dock->dockModel().movePanel(spawned->id, leaf->id);
        }
    }
    _host.dock->fireDockUpdated();
    return _host.dock->activatePanel(tabId);
}

void EditorDockWorkspace::applyDefaultLayout()
{
    if (!_host.dock) {
        return;
    }
    FDockTreeModel& model          = _host.dock->dockModel();
    const DockPanelId viewportId   = dockPanelIdForKey(model, "viewport");
    const DockPanelId hierarchyId  = dockPanelIdForKey(model, "hierarchy");
    const DockPanelId inspectorId  = dockPanelIdForKey(model, "inspector");
    const DockPanelId contentId    = dockPanelIdForKey(model, "content-browser");
    const DockPanelId statsId      = dockPanelIdForKey(model, "frame-stats");
    const DockPanelId workbenchId  = dockPanelIdForKey(model, "gui-workbench");
    const DockPanelId runtimeId    = dockPanelIdForKey(model, "runtime-tools");
    const DockPanelId designerId   = dockPanelIdForKey(model, "ui-designer");
    const DockPanelId assetsId     = dockPanelIdForKey(model, "asset-inspector");
    const DockPanelId debugId      = dockPanelIdForKey(model, "debug-images");
    if (viewportId == kInvalidDockPanelId || hierarchyId == kInvalidDockPanelId || inspectorId == kInvalidDockPanelId ||
        contentId == kInvalidDockPanelId) {
        return;
    }

    model.selectPanel(viewportId);
    const DockNodeId rootLeaf = model.getRootNode()->id;
    model.splitLeaf(rootLeaf, EDockCardinalSide::East, inspectorId, 0.74f);
    if (FDockNode* viewportLeaf = model.findLeafForPanel(viewportId)) {
        model.splitLeaf(viewportLeaf->id, EDockCardinalSide::West, hierarchyId, 0.26f);
    }
    if (FDockNode* viewportLeaf = model.findLeafForPanel(viewportId)) {
        model.splitLeaf(viewportLeaf->id, EDockCardinalSide::South, contentId, 0.72f);
    }
    if (FDockNode* contentLeaf = model.findLeafForPanel(contentId)) {
        if (statsId != kInvalidDockPanelId) {
            model.movePanel(statsId, contentLeaf->id);
        }
        if (workbenchId != kInvalidDockPanelId) {
            model.movePanel(workbenchId, contentLeaf->id);
        }
        if (runtimeId != kInvalidDockPanelId) {
            model.movePanel(runtimeId, contentLeaf->id);
        }
        if (designerId != kInvalidDockPanelId) {
            model.movePanel(designerId, contentLeaf->id);
        }
        if (assetsId != kInvalidDockPanelId) {
            model.movePanel(assetsId, contentLeaf->id);
        }
        if (debugId != kInvalidDockPanelId) {
            model.movePanel(debugId, contentLeaf->id);
        }
        model.selectPanel(contentId);
    }
    model.selectPanel(viewportId);
    model.selectPanel(hierarchyId);
    model.selectPanel(inspectorId);
}

bool EditorDockWorkspace::tryRestoreLayout()
{
    if (!_host.dock) {
        return false;
    }
    nlohmann::json layout = nlohmann::json::object();
    if (!ConfigManager::get().tryGet("editor", "dockLayout", layout)) {
        return false;
    }
    return _host.dock->importLayoutJson(layout);
}

void EditorDockWorkspace::persistLayout()
{
    if (!_host.dock) {
        return;
    }
    ConfigManager::Editor("editor")
        .set("dockLayout", _host.dock->exportLayoutJson())
        .flush();
}

} // namespace ya
