#include "GameEditor/UI/EditorDockWorkspace.h"

#include "Core/Config/ConfigManager.h"
#include "Core/Log.h"
#include "GUI/Widgets/Controls/DockSpace/DockContext.h"
#include "GUI/Widgets/Controls/Menu.h"
#include "GUI/Widgets/Controls/MenuBar.h"
#include "GUI/Widgets/UIElement.h"
#include "GameEditor/UI/EditorViewportHost.h"

#include <nlohmann/json.hpp>
#include <string>
#include <unordered_set>
#include <vector>

namespace ya
{

namespace
{

// Keep in sync with DefaultEditorDockLayout.json.
constexpr std::string_view kFactoryDockLayoutJson = R"JSON(
{
  "version": 1,
  "root": {
    "kind": "split",
    "orientation": "vertical",
    "ratio": 0.74,
    "minExtent": [120.0, 120.0],
    "children": [
      {
        "kind": "split",
        "orientation": "vertical",
        "ratio": 0.26,
        "minExtent": [120.0, 120.0],
        "children": [
          {
            "kind": "leaf",
            "panels": ["hierarchy"],
            "selected": "hierarchy"
          },
          {
            "kind": "split",
            "orientation": "horizontal",
            "ratio": 0.72,
            "minExtent": [120.0, 120.0],
            "children": [
              {
                "kind": "leaf",
                "panels": ["viewport"],
                "selected": "viewport"
              },
              {
                "kind": "leaf",
                "panels": [
                  "content-browser",
                  "frame-stats",
                  "runtime-tools",
                  "ui-designer",
                  "asset-inspector",
                  "debug-images"
                ],
                "selected": "content-browser"
              }
            ]
          }
        ]
      },
      {
        "kind": "leaf",
        "panels": ["inspector"],
        "selected": "inspector"
      }
    ]
  },
  "floating": []
}
)JSON";

} // namespace

const nlohmann::json& EditorDockWorkspace::factoryLayout()
{
    static const nlohmann::json kLayout = nlohmann::json::parse(kFactoryDockLayoutJson);
    return kLayout;
}

FEditorTabSpawnContext EditorDockWorkspace::makeSpawnContext() const
{
    return FEditorTabSpawnContext{
        .tree            = *_host.tree,
        .layer           = *_host.layer,
        .selection       = *_host.selection,
        .actions         = *_host.actions,
        .undo            = *_host.undo,
        .viewportHost    = _host.viewportHost,
    };
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
                const bool bOpen = _host.dock && _host.dock->hasPanel(tabId);
                items.push_back({
                    .label    = spawner.title,
                    .action   = [this, tabId, bOpen]() {
                        if (bOpen) {
                            if (_host.dock) {
                                (void)_host.dock->closePanel(tabId);
                            }
                            return;
                        }
                        invokeTab(tabId);
                    },
                    .bChecked = bOpen,
                });
            }
        }
        if (!items.empty()) {
            items.push_back(UIMenu::FItem::separator());
        }
        items.push_back({
            .label  = "Reset Layout",
            .action = [this]() { resetLayout(); },
        });
        return UIMenu::create(std::move(items));
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
        return _host.dock->importLayoutJson(sanitized);
    };

    if (applyOnce(layout)) {
        return true;
    }
    if (!bFallbackToFactory) {
        return false;
    }

    YA_CORE_WARN("EditorDockWorkspace: layout document failed; applying factory layout");
    const std::vector<std::string> factoryKeys = FDockContext::collectLayoutPanelKeys(factoryLayout());
    std::unordered_set<std::string> factorySet(factoryKeys.begin(), factoryKeys.end());
    for (const std::string& key : _host.dock->panelStableKeys()) {
        if (!factorySet.contains(key)) {
            (void)_host.dock->closePanel(key);
        }
    }
    return applyOnce(factoryLayout());
}

void EditorDockWorkspace::applyWorkspaceLayout()
{
    nlohmann::json user;
    if (ConfigManager::get().tryGet("editor", "dockLayout", user)) {
        if (applyLayoutDocument(user, true)) {
            return;
        }
    }
    (void)applyLayoutDocument(factoryLayout(), false);
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
        YA_CORE_WARN("EditorDockWorkspace: no spawner for tab '{}'", tabId);
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
    _host.dock->fireDockUpdated();
    return _host.dock->activatePanel(tabId);
}

void EditorDockWorkspace::resetLayout()
{
    if (!_host.dock) {
        return;
    }
    for (const std::string& key : _host.dock->panelStableKeys()) {
        (void)_host.dock->closePanel(key);
    }
    (void)applyLayoutDocument(factoryLayout(), false);
    persistLayout();
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
