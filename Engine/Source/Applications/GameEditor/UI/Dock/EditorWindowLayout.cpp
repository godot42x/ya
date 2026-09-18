#include "GameEditor/UI/Dock/EditorWindowLayout.h"

#include "Core/Config/ConfigManager.h"
#include "Core/Log.h"
#include "GUI/Host/GUIWindowChrome.h"
#include "GUI/Host/GUIWindowPlacement.h"
#include "GUI/Host/GUIWindowSession.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/DockSpace/DockContext.h"
#include "GUI/Widgets/Controls/DockSpace/DockSpace.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GameEditor/UI/Dock/EditorDockWorkspace.h"
#include "EditorDockSupport.h"
#include "GameEditor/UI/Shell/EditorDocumentSession.h"
#include "GameEditor/UI/Shell/EditorWindowRegistry.h"

#include <algorithm>
#include <cstddef>

namespace ya
{

namespace
{

void hostDockOnTree(WidgetTree& tree,
                    const std::shared_ptr<FDockContext>& dock,
                    const FWindowChromeLayout& chrome,
                    INativeWindow* native)
{
    hostEditorDockOnTree(tree, dock, chrome, native);
}

[[nodiscard]] const char* roleToJson(EEditorWindowRole role)
{
    return role == EEditorWindowRole::TornOff ? "tornOff" : "main";
}

[[nodiscard]] EEditorWindowRole roleFromJson(const nlohmann::json& record)
{
    if (!record.contains("role") || !record["role"].is_string()) {
        return EEditorWindowRole::TornOff;
    }
    return record["role"].get<std::string>() == "main" ? EEditorWindowRole::Main
                                                       : EEditorWindowRole::TornOff;
}

[[nodiscard]] nlohmann::json boundsJson(const FWindowScreenPlacement& placement)
{
    return nlohmann::json::array({placement.x, placement.y, placement.w, placement.h});
}

FWindowScreenPlacement placementFromRecord(const nlohmann::json& record)
{
    FWindowScreenPlacement placement;
    if (record.contains("bounds") && record["bounds"].is_array() && record["bounds"].size() == 4) {
        placement.x = record["bounds"][0].get<int>();
        placement.y = record["bounds"][1].get<int>();
        placement.w = record["bounds"][2].get<int>();
        placement.h = record["bounds"][3].get<int>();
        placement.bHasOrigin = record.value("hasOrigin", true);
    }
    placement.monitorIndex = record.value("monitor", -1);
    if (record.contains("monitorName") && record["monitorName"].is_string()) {
        placement.monitorName = record["monitorName"].get<std::string>();
    }
    placement.bMaximized = record.value("maximized", false);
    return placement;
}

FWindowScreenPlacement placementFromSession(const EditorWindowSession& session,
                                            INativeWindow*             mainNative,
                                            IGUIWindowCoordinator*     coordinator)
{
    FWindowScreenPlacement placement;
    const EditorWindowMetrics& metrics = session.metrics();
    placement.w            = static_cast<int>(std::max(metrics.logicalExtent.width, 1u));
    placement.h            = static_cast<int>(std::max(metrics.logicalExtent.height, 1u));
    placement.x            = metrics.screenX;
    placement.y            = metrics.screenY;
    placement.monitorIndex = metrics.monitorIndex;
    placement.bMaximized   = metrics.bMaximized;
    placement.bHasOrigin   = metrics.bHasScreenOrigin;

    if (session.windowId() == kDefaultEditorWindowId && mainNative) {
        return queryWindowScreenPlacement(*mainNative);
    }
    if (coordinator && session.hostGuiWindowId() != 0) {
        if (IGUIWindowSession* gui = coordinator->findSession(session.hostGuiWindowId())) {
            if (INativeWindow* native = gui->nativeWindow()) {
                return queryWindowScreenPlacement(*native);
            }
        }
    }
    return placement;
}

[[nodiscard]] bool recordHasDockPanels(const nlohmann::json& windowRoot,
                                       const nlohmann::json& ownedNested)
{
    return (windowRoot.is_object() && !FDockContext::collectLayoutPanelKeys(windowRoot).empty()) ||
           (ownedNested.is_object() && !FDockContext::collectLayoutPanelKeys(ownedNested).empty());
}

[[nodiscard]] EditorRootId inferOwnerEditorId(const EditorWindowSession& session)
{
    static constexpr EditorRootId kRoots[] = {
        kUIEditorRootId,
        kMaterialEditorRootId,
        kScriptEditorRootId,
        kLevelEditorRootId,
    };
    if (FDockContext* root = session.surface().windowRootDock()) {
        for (EditorRootId id : kRoots) {
            const char* tabId = editorRootTabId(id);
            if (tabId && root->hasPanel(tabId)) {
                return id;
            }
        }
        for (const std::string& key : root->panelStableKeys()) {
            if (const FDockContext::FPanel* panel = root->findPanelByStableKey(key)) {
                if (panel->ownerEditorId != 0 && editorRootTabId(panel->ownerEditorId) != nullptr) {
                    return panel->ownerEditorId;
                }
            }
        }
    }
    if (FDockContext* nested = session.surface().ownedNestedDock()) {
        for (const std::string& key : nested->panelStableKeys()) {
            if (const FDockContext::FPanel* panel = nested->findPanelByStableKey(key)) {
                if (panel->ownerEditorId != 0 && editorRootTabId(panel->ownerEditorId) != nullptr) {
                    return panel->ownerEditorId;
                }
            }
        }
    }
    return kInvalidEditorRootId;
}

[[nodiscard]] bool isKnownEditorRoot(uint32_t ownerEditorId)
{
    return ownerEditorId == 0 || editorRootTabId(ownerEditorId) != nullptr;
}

[[nodiscard]] nlohmann::json exportSessionRecord(const EditorWindowSession& session,
                                                 INativeWindow*             mainNative,
                                                 IGUIWindowCoordinator*     coordinator)
{
    const EEditorWindowRole role =
        session.windowId() == kDefaultEditorWindowId ? EEditorWindowRole::Main
                                                     : EEditorWindowRole::TornOff;
    const FWindowScreenPlacement placement = placementFromSession(session, mainNative, coordinator);
    const EditorRootId owner = role == EEditorWindowRole::Main ? kLevelEditorRootId
                                                               : inferOwnerEditorId(session);
    nlohmann::json record;
    record["role"]       = roleToJson(role);
    record["windowId"]   = session.windowId();
    record["bounds"]     = boundsJson(placement);
    record["hasOrigin"]  = placement.bHasOrigin;
    record["monitor"]    = placement.monitorIndex;
    if (!placement.monitorName.empty()) {
        record["monitorName"] = placement.monitorName;
    }
    record["maximized"]    = placement.bMaximized;
    record["activeRootId"] = owner != kInvalidEditorRootId ? owner : session.activeRoot().id();
    if (owner != kInvalidEditorRootId) {
        record["ownerEditorId"] = owner;
        if (const EditorDocumentSession* doc = session.root(owner).document()) {
            record["documentKey"] = doc->id().key;
        }
        else {
            record["documentKey"] = std::string{};
        }
    }
    else {
        record["documentKey"] = std::string{};
    }
    if (FDockContext* root = session.surface().windowRootDock()) {
        record["windowRoot"] = root->exportLayoutJson();
    }
    if (FDockContext* nested = session.surface().ownedNestedDock()) {
        record["ownedNested"] = nested->exportLayoutJson();
    }
    if (coordinator && session.hostGuiWindowId() != 0) {
        if (const IGUIWindowSession* gui = coordinator->findSession(session.hostGuiWindowId())) {
            if (gui->closeRequested()) {
                record["closing"] = true;
            }
        }
    }
    return record;
}

[[nodiscard]] nlohmann::json windowField(const nlohmann::json& record, EEditorTabPlacement placement)
{
    const char* field = placement == EEditorTabPlacement::EditorOwnedNested ? "ownedNested"
                                                                            : "windowRoot";
    if (record.contains(field) && record[field].is_object()) {
        return record[field];
    }
    return {};
}

} // namespace

nlohmann::json exportEditorWindowLayout(const EditorWindowRegistry& windows,
                                        INativeWindow*              mainNative,
                                        IGUIWindowCoordinator*      coordinator)
{
    nlohmann::json document;
    document["version"] = kEditorWindowLayoutVersion;
    nlohmann::json list = nlohmann::json::array();
    windows.forEach([&](const EditorWindowSession& session) {
        const bool bMain = session.windowId() == kDefaultEditorWindowId;
        if (!bMain && !sessionHasDockPanels(session)) {
            return;
        }
        list.push_back(exportSessionRecord(session, mainNative, coordinator));
    });
    document["windows"] = std::move(list);
    return document;
}

void persistEditorWindowLayout(const EditorWindowRegistry& windows,
                               INativeWindow*              mainNative,
                               IGUIWindowCoordinator*      coordinator)
{
    ConfigManager::Editor("editor")
        .set("dockLayout", exportEditorWindowLayout(windows, mainNative, coordinator))
        .flush();
}

const nlohmann::json* findMainEditorWindowRecord(const nlohmann::json& document)
{
    if (!document.is_object() || !document.contains("windows") || !document["windows"].is_array()) {
        return nullptr;
    }
    const nlohmann::json* first = nullptr;
    for (const nlohmann::json& record : document["windows"]) {
        if (!record.is_object()) {
            continue;
        }
        if (!first) {
            first = &record;
        }
        if (roleFromJson(record) == EEditorWindowRole::Main) {
            return &record;
        }
    }
    return first;
}

size_t restoreEditorExtraWindows(FEditorNativeTearOff&      env,
                                 const nlohmann::json&      document,
                                 FEditorWindowRestoreStats* stats)
{
    if (!env.coordinator || !env.windows) {
        return 0;
    }
    if (!document.is_object() || document.value("version", 1) < kEditorWindowLayoutVersion ||
        !document.contains("windows") || !document["windows"].is_array()) {
        return 0;
    }

    FEmptyGuiDelegate fallback;
    IGUIAppDelegate& content = env.content ? *env.content : fallback;
    size_t restored = 0;
    for (const nlohmann::json& record : document["windows"]) {
        if (!record.is_object() || roleFromJson(record) == EEditorWindowRole::Main) {
            continue;
        }
        if (record.value("closing", false)) {
            if (stats) {
                ++stats->skippedClosing;
            }
            continue;
        }

        const uint32_t ownerEditorId = record.value("ownerEditorId", 0u);
        if (!isKnownEditorRoot(ownerEditorId)) {
            if (stats) {
                ++stats->skippedMissingOwner;
            }
            continue;
        }

        const std::string documentKey = record.value("documentKey", std::string{});
        EditorDocumentSession* documentSession = nullptr;
        if (!documentKey.empty() && env.documents) {
            FEditorDocumentId wanted;
            wanted.kind = editorDocumentKindForRoot(ownerEditorId);
            wanted.key  = documentKey;
            documentSession = env.documents->find(wanted);
            if (!documentSession) {
                if (stats) {
                    ++stats->skippedMissingDocument;
                }
                continue;
            }
        }

        const nlohmann::json windowRoot = windowField(record, EEditorTabPlacement::WindowRootDock);
        const nlohmann::json ownedNested = windowField(record, EEditorTabPlacement::EditorOwnedNested);
        if (!recordHasDockPanels(windowRoot, ownedNested)) {
            if (stats) {
                ++stats->skippedEmpty;
            }
            continue;
        }

        const FWindowScreenPlacement placement = placementFromRecord(record);
        FGUIWindowHostConfig config;
        config.title        = "Editor";
        config.width        = static_cast<uint32_t>(std::max(placement.w, 1));
        config.height       = static_cast<uint32_t>(std::max(placement.h, 1));
        config.posX         = placement.x;
        config.posY         = placement.y;
        config.monitorIndex = placement.monitorIndex;
        config.bHasPosition = placement.bHasOrigin;
        config.bMaximized   = placement.bMaximized;
        const GUIWindowId guiId = env.coordinator->createSession(config, content, env.render);
        if (guiId == 0) {
            YA_CORE_WARN("EditorWindowLayout: failed to restore extra native window");
            continue;
        }
        IGUIWindowSession* guiSession = env.coordinator->findSession(guiId);
        if (!guiSession || !guiSession->tree()) {
            env.coordinator->destroySession(guiId);
            continue;
        }
        if (INativeWindow* native = guiSession->nativeWindow()) {
            const FWindowPlacementApplyResult recovered = recoverWindowScreenPlacement(*native, placement);
            if (recovered.recovery == EWindowPlacementRecovery::Relocated && stats) {
                ++stats->relocatedMonitor;
            }
        }

        EditorWindowId editorId = record.value("windowId", 0u);
        if (editorId == 0 || editorId == kDefaultEditorWindowId || env.windows->find(editorId)) {
            editorId = kDefaultEditorWindowId + 1;
            while (env.windows->find(editorId)) {
                ++editorId;
            }
        }
        EditorWindowSession* extra = env.windows->create(editorId);
        if (!extra) {
            env.coordinator->destroySession(guiId);
            continue;
        }
        extra->adoptHostTree(guiSession->tree(), guiId);

        if (ownerEditorId != 0 && documentSession) {
            extra->root(ownerEditorId).bindDocument(documentSession);
        }

        std::shared_ptr<FDockContext> rootDock = extra->surface().windowRootDockPtr();
        std::shared_ptr<FDockContext> nestedDock = extra->surface().ownedNestedDockPtr();
        if (rootDock) {
            rootDock->bAllowDocking  = true;
            rootDock->bAllowFloating = true;
            rootDock->bAllowTearOff  = true;
            rootDock->sourceScope    = EDockSourceScope::WindowRoot;
            rootDock->hostWindowId   = guiId;
        }
        if (nestedDock) {
            nestedDock->bAllowDocking  = true;
            nestedDock->bAllowFloating = true;
            nestedDock->bAllowTearOff  = true;
            nestedDock->sourceScope    = EDockSourceScope::EditorOwned;
            nestedDock->hostWindowId   = guiId;
        }

        EditorDockWorkspace rootHost;
        rootHost.bind({
            .spawners        = env.spawners,
            .dock            = rootDock.get(),
            .activeRootId    = static_cast<EditorRootId>(record.value("activeRootId", kLevelEditorRootId)),
            .targetPlacement = EEditorTabPlacement::WindowRootDock,
            .windowId        = extra->windowId(),
        });
        EditorDockWorkspace nestedHost;
        nestedHost.bind({
            .spawners        = env.spawners,
            .dock            = nestedDock.get(),
            .activeRootId    = ownerEditorId != 0 ? static_cast<EditorRootId>(ownerEditorId)
                                                  : kLevelEditorRootId,
            .targetPlacement = EEditorTabPlacement::EditorOwnedNested,
            .windowId        = extra->windowId(),
        });

        bool bApplied = false;
        if (windowRoot.is_object() && !FDockContext::collectLayoutPanelKeys(windowRoot).empty()) {
            bApplied = rootHost.applyLayoutDocument(windowRoot, false) || bApplied;
        }
        if (ownedNested.is_object() && !FDockContext::collectLayoutPanelKeys(ownedNested).empty()) {
            bApplied = nestedHost.applyLayoutDocument(ownedNested, false) || bApplied;
        }
        const bool bHasPanels =
            (rootDock && !rootDock->panelStableKeys().empty()) ||
            (nestedDock && !nestedDock->panelStableKeys().empty());
        if (!bApplied || !bHasPanels) {
            extra->adoptHostTree(nullptr);
            env.windows->destroy(editorId);
            env.coordinator->destroySession(guiId);
            if (stats) {
                ++stats->skippedEmpty;
            }
            continue;
        }

        const bool bOwnedOnly = (rootDock && rootDock->panelStableKeys().empty()) &&
                                nestedDock && !nestedDock->panelStableKeys().empty();
        hostDockOnTree(*guiSession->tree(),
                       bOwnedOnly ? nestedDock : rootDock,
                       guiSession->chrome().layout,
                       guiSession->nativeWindow());
        ++restored;
        if (stats) {
            ++stats->restored;
        }
    }
    return restored;
}

bool recoverEditorWindowPlacement(INativeWindow& native, const nlohmann::json& windowRecord)
{
    return recoverWindowScreenPlacement(native, placementFromRecord(windowRecord)).bApplied;
}

} // namespace ya
