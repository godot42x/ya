#include "GameEditor/UI/Dock/EditorNativeTearOff.h"

#include "GUI/Declarative/Build.h"
#include "GUI/Host/GUIDockNativePlacement.h"
#include "GUI/Host/GUIWindowChrome.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/DockSpace/DockContext.h"
#include "GUI/Widgets/Controls/DockSpace/DockNode.h"
#include "GUI/Widgets/Controls/DockSpace/DockSpace.h"
#include "GUI/Widgets/Controls/Border.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/TabBar.h"
#include "GUI/Widgets/DragDropOperation.h"
#include "GUI/Widgets/UIBehavior.h"
#include "GUI/Widgets/UIElement.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GameEditor/UI/Dock/EditorDockWorkspace.h"
#include "EditorDockSupport.h"
#include "GameEditor/UI/Dock/EditorNestedDockHost.h"
#include "RHI/NativeWindow.h"

#include <memory>
#include <optional>
#include <vector>

namespace ya
{

namespace
{

[[nodiscard]] EditorWindowId nextFreeEditorWindowId(EditorWindowRegistry& windows, GUIWindowId guiId)
{
    if (guiId != 0 && windows.find(static_cast<EditorWindowId>(guiId)) == nullptr) {
        return static_cast<EditorWindowId>(guiId);
    }
    EditorWindowId id = kDefaultEditorWindowId + 1;
    while (windows.find(id) != nullptr) {
        ++id;
    }
    return id;
}

[[nodiscard]] FEditorTabDragPayload payloadFromPanel(FDockContext& sourceDock,
                                                     const FDockContext::FPanel& panel,
                                                     const FDockPanelRecord& record,
                                                     EditorWindowSession& sourceSession,
                                                     const EditorTabSpawnerRegistry* spawners)
{
    const FEditorTabSpawner* spawner = spawners ? spawners->find(record.stableKey) : nullptr;
    if (spawner) {
        return makeEditorTabDragPayload(*spawner,
                                        panel.documentKey,
                                        sourceSession.windowId(),
                                        panel.ownerEditorId);
    }

    FEditorTabDragPayload payload;
    payload.tabId = record.stableKey;
    payload.documentKey = panel.documentKey;
    payload.ownerEditorId = panel.ownerEditorId;
    payload.sourceWindowId = sourceSession.windowId();
    if (sourceDock.sourceScope == EDockSourceScope::EditorOwned) {
        payload.scope = EEditorTabScope::EditorOwnedTool;
        payload.detachPolicy = EEditorTabDetachPolicy::TearOffKeepOwner;
        payload.sourcePlacement = EEditorTabPlacement::EditorOwnedNested;
    }
    else if (panel.ownerEditorId != kInvalidEditorRootId) {
        payload.scope = EEditorTabScope::WindowRootEditor;
        payload.detachPolicy = EEditorTabDetachPolicy::IndependentWindow;
        payload.sourcePlacement = EEditorTabPlacement::WindowRootDock;
    }
    else {
        payload.scope = EEditorTabScope::WindowTool;
        payload.detachPolicy = EEditorTabDetachPolicy::IndependentWindow;
        payload.sourcePlacement = EEditorTabPlacement::WindowRootDock;
    }
    return payload;
}

[[nodiscard]] bool editorWindowHasOnlyPanel(const EditorWindowSession& session, DockPanelId panelId)
{
    if (!canCloseEditorWindow(session.windowId())) {
        return false;
    }
    size_t count = 0;
    bool bHasThis = false;
    const auto tally = [&](const FDockContext* dock) {
        if (!dock) {
            return;
        }
        for (const std::string& key : dock->panelStableKeys()) {
            ++count;
            if (const FDockContext::FPanel* panel = dock->findPanelByStableKey(key);
                panel && panel->id == panelId) {
                bHasThis = true;
            }
        }
    };
    tally(session.surface().windowRootDock());
    tally(session.surface().ownedNestedDock());
    return count == 1 && bHasThis;
}

[[nodiscard]] FDockContext* nestedDockForRoot(EditorWindowSession& session, EditorRootId rootId)
{
    if (rootId == kLevelEditorRootId || rootId == kInvalidEditorRootId) {
        return session.surface().ownedNestedDock();
    }
    FDockContext* rootDock = session.surface().windowRootDock();
    const char* tabId = editorRootTabId(rootId);
    if (!rootDock || !tabId) {
        return nullptr;
    }
    const FDockContext::FPanel* panel = rootDock->findPanelByStableKey(tabId);
    if (!panel || !panel->widget) {
        return nullptr;
    }
    if (auto* host = dynamic_cast<EditorNestedDockHost*>(panel->widget.get())) {
        return host->nestedDock();
    }
    return nullptr;
}

[[nodiscard]] FDockContext* findHomeDock(EditorWindowRegistry& windows,
                                         const FEditorTabDragPayload& payload,
                                         FDockContext* sourceDock)
{
    FDockContext* home = nullptr;
    windows.forEach([&](EditorWindowSession& session) {
        if (home) {
            return;
        }
        FDockContext* candidate = payload.scope == EEditorTabScope::EditorOwnedTool
                                      ? nestedDockForRoot(session, payload.ownerEditorId)
                                      : session.surface().windowRootDock();
        if (candidate && candidate != sourceDock) {
            home = candidate;
        }
    });
    return home;
}

void bindHomeAdoptPolicy(FDockContext& home,
                         const FEditorTabDragPayload& payload,
                         EditorTabSpawnerRegistry* spawners,
                         EditorWindowId windowId)
{
    EditorDockWorkspace policyHost;
    policyHost.bind({
        .spawners        = spawners,
        .dock            = &home,
        .activeRootId    = homeRootFor(payload),
        .targetPlacement = homePlacementFor(payload),
        .windowId        = windowId,
    });
}

constexpr const char* kTornTitleBarName = "EditorTornTitleBar";
constexpr const char* kTornPageTabsName = "EditorTornPageTabs";
constexpr const char* kTornDockName     = "EditorTornDock";
constexpr float       kTornTitleFallbackHeight = 28.0f;

[[nodiscard]] float editorTornTitleHeight(const FWindowChromeLayout& chrome)
{
    return chrome.contentInsets.top > 0.0f ? chrome.contentInsets.top : kTornTitleFallbackHeight;
}

[[nodiscard]] bool isTornChromeWidget(const UIElement& widget)
{
    return widget._name == kTornTitleBarName || widget._name == kTornPageTabsName;
}

struct FEditorTornChromeState
{
    std::shared_ptr<FDockContext> dock;
    UITabBar*                     tabBar = nullptr;
    WidgetTree*                   tree   = nullptr;
    INativeWindow*                native = nullptr;
    std::vector<std::string>      tabKeys;
};

void publishTornTitleClientHits(FEditorTornChromeState& state)
{
    if (!state.native || !state.tabBar) {
        return;
    }
    std::vector<FWindowChromeRect> hits;
    const Rect2D& rect = state.tabBar->getLayoutRect();
    if (rect.extent.x > 0.0f && rect.extent.y > 0.0f) {
        hits.push_back(FWindowChromeRect{rect.pos.x, rect.pos.y, rect.extent.x, rect.extent.y});
    }
    updateWindowChromeTitleClientHits(*state.native, hits);
}

struct FEditorTornTitleHitsBehavior final : UIBehavior
{
    std::shared_ptr<FEditorTornChromeState> state;

    [[nodiscard]] bool wantsTick() const override { return state && state->native && state->tabBar; }
    void tick(UIElement&, float) override
    {
        if (state) {
            publishTornTitleClientHits(*state);
        }
    }
};

void syncTornWindowTabs(FEditorTornChromeState& state)
{
    if (!state.tabBar || !state.dock) {
        return;
    }

    const std::vector<DockNodeId> leaves = state.dock->dockModel().leafIds();
    if (leaves.size() == 1) {
        (void)state.dock->dockModel().setHideTabBar(leaves.front(), true);
    }
    else {
        for (const DockNodeId leafId : leaves) {
            const FDockNode* leaf = state.dock->dockModel().findNode(leafId);
            if (!leaf || leaf->kind != EDockNodeKind::Stack || leaf->leafRole == EDockLeafRole::Page) {
                continue;
            }
            (void)state.dock->dockModel().setHideTabBar(leafId, false);
        }
    }

    std::vector<std::string> keys;
    std::vector<std::string> titles;
    std::vector<bool> closable;
    int selected = -1;
    const DockNodeId focused = state.dock->lastFocusedLeafId();
    const FDockNode* focusLeaf = focused != kInvalidDockNodeId ? state.dock->dockModel().findNode(focused)
                                                               : nullptr;
    for (const std::string& key : state.dock->panelStableKeys()) {
        const FDockContext::FPanel* panel = state.dock->findPanelByStableKey(key);
        const FDockPanelRecord* record = panel ? state.dock->dockModel().findPanel(panel->id) : nullptr;
        if (!panel || !record) {
            continue;
        }
        if (focusLeaf && focusLeaf->kind == EDockNodeKind::Stack &&
            panel->id == focusLeaf->selectedPanel) {
            selected = static_cast<int>(keys.size());
        }
        else if (selected < 0) {
            if (const FDockNode* leaf = state.dock->dockModel().findLeafForPanel(panel->id);
                leaf && leaf->selectedPanel == panel->id) {
                selected = static_cast<int>(keys.size());
            }
        }
        keys.push_back(key);
        titles.push_back(record->title);
        closable.push_back(record->closable);
    }

    if (keys != state.tabKeys || state.tabBar->tabCount() != static_cast<int>(keys.size())) {
        state.tabBar->clearTabs();
        state.tabKeys = keys;
        for (size_t i = 0; i < keys.size(); ++i) {
            UITabButton* button = state.tabBar->addTab(titles[i]);
            button->_bClosable = closable[i];
            if (closable[i]) {
                const std::string key = keys[i];
                button->_onClose = [dock = state.dock, key]() {
                    if (dock) {
                        (void)dock->closePanel(key);
                    }
                };
            }
        }
    }
    if (selected >= 0) {
        state.tabBar->syncSelectedTab(selected);
    }
    publishTornTitleClientHits(state);
}

void beginTornWindowTabDrag(FEditorTornChromeState& state, int index)
{
    if (!state.tree || !state.dock || !state.tabBar ||
        index < 0 || index >= static_cast<int>(state.tabKeys.size())) {
        return;
    }
    if (UITabButton* button = state.tabBar->tabAt(index);
        !button || !state.tabBar->allowsArmedTabDrag(*button)) {
        return;
    }
    const FDockContext::FPanel* panel = state.dock->findPanelByStableKey(state.tabKeys[static_cast<size_t>(index)]);
    const FDockPanelRecord* record = panel ? state.dock->dockModel().findPanel(panel->id) : nullptr;
    if (!panel || !record) {
        return;
    }
    const DockPanelId panelId = panel->id;
    std::string label = record->title;
    std::shared_ptr<FDockContext> dock = state.dock;
    auto operation = FDockPanelDragDropOp::make(panelId, std::move(label), dock.get());
    if (dock->panelStableKeys().size() == 1) {
        operation->bHideSourceWindowOnLeave = true;
    }
    DragSessionObserver observer;
    observer.onFinished = [dock, panelId](EDragFinishResult result, const glm::vec2& logicalPoint, std::string_view) {
        if (result == EDragFinishResult::Dropped || result == EDragFinishResult::Cancelled) {
            return;
        }
        if (logicalPoint.x < -10000.0f || logicalPoint.y < -10000.0f) {
            return;
        }
        if (result == EDragFinishResult::NoTarget && dock && dock->bAllowTearOff) {
            const glm::vec2 size{320.0f, 240.0f};
            bool bHandled = false;
            if (dock->realizeNoTargetTearOff) {
                bHandled = dock->realizeNoTargetTearOff(panelId, logicalPoint, size);
            }
            if (!bHandled) {
                dock->tearOffPanel(panelId, logicalPoint, size);
            }
            dock->fireFloatingUpdated();
            dock->notifyDockLayoutListeners();
        }
    };
    state.tree->beginDrag(state.tabBar, std::move(operation), std::move(observer));
}

[[nodiscard]] bool acceptTornWindowTabDrop(FEditorTornChromeState& state,
                                           const UIDragDropOperation& operation)
{
    const auto* dockOp = operation.as<FDockPanelDragDropOp>();
    if (!dockOp || !state.dock || dockOp->panelId == kInvalidDockPanelId) {
        return false;
    }
    const FDockContext* source = dockOp->sourceContext ? dockOp->sourceContext : state.dock.get();
    const FDockPanelRecord* record = source->dockModel().findPanel(dockOp->panelId);
    const FDockContext::FPanel* panel = source->findPanel(dockOp->panelId);
    if (!record || !panel) {
        return false;
    }
    if (source == state.dock.get()) {
        return true;
    }
    return state.dock->acceptsImportedPanel(record->stableKey, panel->ownerEditorId, panel->documentKey);
}

void dropOntoTornWindowTabs(FEditorTornChromeState& state, const UIDragDropOperation& operation)
{
    const auto* dockOp = operation.as<FDockPanelDragDropOp>();
    if (!dockOp || !state.dock) {
        return;
    }
    DockPanelId panelId = dockOp->panelId;
    const bool bImport = dockOp->sourceContext && dockOp->sourceContext != state.dock.get();
    if (bImport) {
        std::optional<FDockContext::FDockExtractedPanel> extracted =
            dockOp->sourceContext->extractPanel(panelId);
        if (!extracted) {
            return;
        }
        panelId = state.dock->adoptPanel(std::move(*extracted));
        if (panelId == kInvalidDockPanelId) {
            return;
        }
    }
    DockNodeId leafId = state.dock->lastFocusedLeafId();
    if (leafId == kInvalidDockNodeId) {
        const std::vector<DockNodeId> leaves = state.dock->dockModel().leafIds();
        leafId = leaves.empty() ? kInvalidDockNodeId : leaves.front();
    }
    if (state.dock->dockModel().findLeafForPanel(panelId)) {
        (void)state.dock->dockModel().movePanel(panelId, leafId);
    }
    else {
        (void)state.dock->dockModel().addPanel(panelId, leafId);
    }
    if (const FDockPanelRecord* record = state.dock->dockModel().findPanel(panelId)) {
        (void)state.dock->activatePanel(record->stableKey);
    }
    else {
        (void)state.dock->dockModel().selectPanel(panelId);
    }
    state.dock->fireDockUpdated();
}

} // namespace

void applyEditorDockFillSlots(WidgetTree& tree, const FWindowChromeLayout& chrome)
{
    UIElement* layer = tree.getLayer(WidgetTree::ELayer::Content);
    if (!layer) {
        return;
    }
    const float titleH = editorTornTitleHeight(chrome);
    FCanvasSlotArgs fill;
    fill.anchorMin      = {0.0f, 0.0f};
    fill.anchorMax      = {1.0f, 1.0f};
    fill.widthSizeMode  = EWidgetSizeMode::Fixed;
    fill.heightSizeMode = EWidgetSizeMode::Fixed;
    fill.offsets        = FMargin{0.0f, titleH, 0.0f, 0.0f};
    for (const UIElementRef& child : layer->getChildren()) {
        if (!child || isTornChromeWidget(*child)) {
            continue;
        }
        if (UISlot* edge = layer->getSlotForChild(*child)) {
            (void)edge->applyArgs(fill);
        }
    }
    tree.invalidateLayout();
}

void hostEditorDockOnTree(WidgetTree& tree,
                          const std::shared_ptr<FDockContext>& dock,
                          const FWindowChromeLayout& chrome,
                          INativeWindow* native)
{
    if (!dock) {
        return;
    }
    UIElement* layer = tree.getLayer(WidgetTree::ELayer::Content);
    if (!layer) {
        return;
    }

    const float titleH = editorTornTitleHeight(chrome);
    auto titleBar = std::make_shared<UICanvasPanel>(kTornTitleBarName);
    titleBar->setVisibility(EWidgetVisibility::SelfHitTestInvisible);
    FCanvasSlotArgs titleArgs;
    titleArgs.anchorMin      = {0.0f, 0.0f};
    titleArgs.anchorMax      = {1.0f, 0.0f};
    titleArgs.widthSizeMode  = EWidgetSizeMode::Fixed;
    titleArgs.heightSizeMode = EWidgetSizeMode::Fixed;
    titleArgs.fixedSize      = {0.0f, titleH};
    (void)tree.attach(*layer, titleBar, titleArgs);
    (void)ui::attach(tree,
                     *titleBar,
                     ui::border("TornTitleBarFill")
                         .setStyleKey("panel.titlebar")
                         .setVisibility(EWidgetVisibility::HitTestInvisible)
                         .release(),
                     ui::canvasSlot().fill());

    auto tabBar = std::make_shared<UITabBar>(kTornPageTabsName);
    tabBar->_bDraggableTabs = true;
    tabBar->_emptyPlaceholder = "Tabs";
    auto state = std::make_shared<FEditorTornChromeState>();
    state->dock   = dock;
    state->tabBar = tabBar.get();
    state->tree   = &tree;
    state->native = native;
    tabBar->_onTabSelected = [state](int index) {
        if (!state->dock || index < 0 || index >= static_cast<int>(state->tabKeys.size())) {
            return;
        }
        (void)state->dock->activatePanel(state->tabKeys[static_cast<size_t>(index)]);
    };
    tabBar->_onTabDragBegin = [state](int index, const std::string&) {
        beginTornWindowTabDrag(*state, index);
    };
    tabBar->_onTabReordered = [state](int from, int to) {
        if (!state->dock || from < 0 || to < 0 ||
            from >= static_cast<int>(state->tabKeys.size()) ||
            to >= static_cast<int>(state->tabKeys.size())) {
            return;
        }
        const FDockContext::FPanel* panel =
            state->dock->findPanelByStableKey(state->tabKeys[static_cast<size_t>(from)]);
        const FDockContext::FPanel* dest =
            state->dock->findPanelByStableKey(state->tabKeys[static_cast<size_t>(to)]);
        if (!panel || !dest) {
            return;
        }
        const FDockNode* leaf = state->dock->dockModel().findLeafForPanel(panel->id);
        if (!leaf || state->dock->dockModel().findLeafForPanel(dest->id) != leaf) {
            return;
        }
        int leafFrom = -1;
        int leafTo   = -1;
        for (int i = 0; i < static_cast<int>(leaf->panelIds.size()); ++i) {
            if (leaf->panelIds[static_cast<size_t>(i)] == panel->id) {
                leafFrom = i;
            }
            if (leaf->panelIds[static_cast<size_t>(i)] == dest->id) {
                leafTo = i;
            }
        }
        if (leafFrom < 0 || leafTo < 0) {
            return;
        }
        const size_t insert = static_cast<size_t>(leafTo > leafFrom ? leafTo + 1 : leafTo);
        if (!state->dock->dockModel().movePanel(panel->id, leaf->id, insert, false)) {
            return;
        }
        std::string key = state->tabKeys[static_cast<size_t>(from)];
        state->tabKeys.erase(state->tabKeys.begin() + from);
        state->tabKeys.insert(state->tabKeys.begin() + to, std::move(key));
        state->dock->notifyDockLayoutListeners();
    };
    tabBar->_onStripDoubleClick = [native]() {
        if (native) {
            (void)toggleWindowChromeTitleZoom(*native);
        }
    };
    {
        auto drop = std::make_shared<UIDropTargetBehavior>();
        drop->canAccept = [state](UIElement&, const UIDragDropOperation& operation, const glm::vec2&) {
            return acceptTornWindowTabDrop(*state, operation);
        };
        drop->canPreview = drop->canAccept;
        drop->handleDrop = [state](UIElement&, const UIDragDropOperation& operation, const glm::vec2&) {
            dropOntoTornWindowTabs(*state, operation);
        };
        tabBar->addBehavior(drop);
    }
    auto hits = std::make_shared<FEditorTornTitleHitsBehavior>();
    hits->state = state;
    tabBar->addBehavior(hits);

    FCanvasSlotArgs tabArgs;
    tabArgs.anchorMin      = {0.0f, 0.0f};
    tabArgs.anchorMax      = {1.0f, 0.0f};
    tabArgs.widthSizeMode  = EWidgetSizeMode::Fixed;
    tabArgs.heightSizeMode = EWidgetSizeMode::Fixed;
    tabArgs.fixedSize      = {0.0f, titleH};
    tabArgs.offsets        = FMargin{chrome.contentInsets.left, 0.0f, chrome.dragRegion.width, 0.0f};
    (void)tree.attach(*layer, tabBar, tabArgs);

    auto space = std::make_shared<UIDockSpace>(kTornDockName);
    FCanvasSlotArgs fill;
    fill.anchorMin      = {0.0f, 0.0f};
    fill.anchorMax      = {1.0f, 1.0f};
    fill.widthSizeMode  = EWidgetSizeMode::Fixed;
    fill.heightSizeMode = EWidgetSizeMode::Fixed;
    fill.offsets        = FMargin{0.0f, titleH, 0.0f, 0.0f};
    (void)tree.attach(*layer, space, fill);
    space->setContext(dock);

    dock->appendOnDockUpdated([weak = std::weak_ptr<FEditorTornChromeState>(state)]() {
        if (auto locked = weak.lock()) {
            syncTornWindowTabs(*locked);
        }
    });
    syncTornWindowTabs(*state);
}

FEditorTabDragPayload makeEditorTabDragPayload(const FEditorTabSpawner& spawner,
                                               std::string_view documentKey,
                                               EditorWindowId sourceWindowId,
                                               EditorRootId ownerOverride)
{
    FEditorTabDragPayload payload;
    payload.tabId = spawner.tabId;
    payload.scope = spawner.scope;
    payload.ownerEditorId = ownerOverride != kInvalidEditorRootId ? ownerOverride : spawner.ownerEditorId;
    payload.documentKey = documentKey.empty() ? spawner.documentKey : std::string(documentKey);
    payload.detachPolicy = spawner.detachPolicy;
    payload.sourceWindowId = sourceWindowId;
    payload.sourcePlacement = spawner.placement;
    return payload;
}

bool reclaimEditorWindowIfEmpty(FEditorNativeTearOff& env, EditorWindowId editorWindowId)
{
    if (!env.windows || !canCloseEditorWindow(editorWindowId)) {
        return false;
    }
    EditorWindowSession* session = env.windows->find(editorWindowId);
    if (!session || sessionHasDockPanels(*session)) {
        return false;
    }
    const GUIWindowId guiId = session->hostGuiWindowId();
    session->adoptHostTree(nullptr, 0);
    if (!env.windows->destroy(editorWindowId)) {
        return false;
    }
    if (env.coordinator && guiId != 0) {
        (void)env.coordinator->destroySession(guiId);
    }
    return true;
}

FEditorRedockResult redockEditorPanelToOwner(FEditorNativeTearOff& env,
                                             EditorWindowSession& sourceSession,
                                             FDockContext& sourceDock,
                                             DockPanelId panelId)
{
    FEditorRedockResult result;
    if (!env.windows) {
        return result;
    }
    const FDockContext::FPanel* panel = sourceDock.findPanel(panelId);
    const FDockPanelRecord* record = sourceDock.dockModel().findPanel(panelId);
    if (!panel || !record) {
        return result;
    }
    const FEditorTabDragPayload payload =
        payloadFromPanel(sourceDock, *panel, *record, sourceSession, env.spawners);
    if (!canRedockEditorTab(payload)) {
        return result;
    }
    FDockContext* home = findHomeDock(*env.windows, payload, &sourceDock);
    if (!home || home->hasPanel(payload.tabId)) {
        return result;
    }
    bindHomeAdoptPolicy(*home, payload, env.spawners, env.windows->defaultSession().windowId());
    const DockPanelId moved = sourceDock.transferPanelTo(*home, panelId);
    if (moved == kInvalidDockPanelId) {
        return result;
    }
    result.targetPanelId = moved;
    result.bReclaimedSourceWindow = reclaimEditorWindowIfEmpty(env, sourceSession.windowId());
    return result;
}

EEditorWindowCloseResult closeEditorWindow(FEditorNativeTearOff& env, EditorWindowId editorWindowId)
{
    if (!env.windows || !canCloseEditorWindow(editorWindowId)) {
        return EEditorWindowCloseResult::RejectedLocked;
    }
    EditorWindowSession* session = env.windows->find(editorWindowId);
    if (!session) {
        return EEditorWindowCloseResult::Failed;
    }
    if (!sessionHasDockPanels(*session)) {
        return reclaimEditorWindowIfEmpty(env, editorWindowId) ? EEditorWindowCloseResult::Reclaimed
                                                               : EEditorWindowCloseResult::Failed;
    }

    struct FPending
    {
        FDockContext* dock = nullptr;
        DockPanelId   id   = kInvalidDockPanelId;
    };
    std::vector<FPending> pending;
    const auto collect = [&](FDockContext* dock) {
        if (!dock) {
            return;
        }
        for (const std::string& key : dock->panelStableKeys()) {
            if (const FDockContext::FPanel* panel = dock->findPanelByStableKey(key)) {
                pending.push_back({dock, panel->id});
            }
        }
    };
    collect(session->surface().windowRootDock());
    collect(session->surface().ownedNestedDock());

    for (const FPending& item : pending) {
        const FDockContext::FPanel* panel = item.dock->findPanel(item.id);
        const FDockPanelRecord* record = item.dock->dockModel().findPanel(item.id);
        if (!panel || !record) {
            return EEditorWindowCloseResult::Failed;
        }
        const FEditorTabDragPayload payload =
            payloadFromPanel(*item.dock, *panel, *record, *session, env.spawners);
        if (!canRedockEditorTab(payload)) {
            return EEditorWindowCloseResult::RejectedLocked;
        }
        FDockContext* home = findHomeDock(*env.windows, payload, item.dock);
        if (!home || home->hasPanel(payload.tabId) ||
            !home->acceptsImportedPanel(record->stableKey, panel->ownerEditorId, panel->documentKey)) {
            return EEditorWindowCloseResult::Failed;
        }
    }

    for (const FPending& item : pending) {
        const FEditorRedockResult moved = redockEditorPanelToOwner(env, *session, *item.dock, item.id);
        if (moved.targetPanelId == kInvalidDockPanelId) {
            return EEditorWindowCloseResult::Failed;
        }
        if (moved.bReclaimedSourceWindow) {
            return EEditorWindowCloseResult::Reclaimed;
        }
    }
    return reclaimEditorWindowIfEmpty(env, editorWindowId) ? EEditorWindowCloseResult::Reclaimed
                                                           : EEditorWindowCloseResult::Failed;
}

FEditorTearOffResult tearOffEditorPanelToNativeWindow(FEditorNativeTearOff& env,
                                                      EditorWindowSession& sourceSession,
                                                      FDockContext& sourceDock,
                                                      DockPanelId panelId,
                                                      const FEditorTearOffGeometry& geometry)
{
    FEditorTearOffResult result;
    if (!env.coordinator || !env.windows) {
        return result;
    }
    const FDockContext::FPanel* panel = sourceDock.findPanel(panelId);
    const FDockPanelRecord* record = sourceDock.dockModel().findPanel(panelId);
    if (!panel || !record) {
        return result;
    }

    const FEditorTabDragPayload payload =
        payloadFromPanel(sourceDock, *panel, *record, sourceSession, env.spawners);
    if (!canTearOffEditorTab(payload)) {
        return result;
    }

    FDockFloatingWindowId placementId = sourceDock.tearOffPanel(panelId,
                                                                geometry.pos,
                                                                geometry.size,
                                                                EDockFloatingProjection::NativeWindow);
    if (placementId == kInvalidFloatingWindowId) {
        return result;
    }
    if (const FDockContext::FDockFloatingPlacement* placement = sourceDock.findFloatingById(placementId);
        placement && placement->projection != EDockFloatingProjection::NativeWindow) {
        if (!sourceDock.setFloatingProjection(placementId, EDockFloatingProjection::NativeWindow)) {
            return result;
        }
    }
    if (geometry.bScreenSpace) {
        (void)sourceDock.setFloatingGeometrySpace(placementId, EDockGeometrySpace::Screen);
    }

    FEmptyGuiDelegate fallback;
    IGUIAppDelegate& content = env.content ? *env.content : fallback;
    // Editor-owned window: keep the transparent title bar the editor opted
    // into on its other windows (the framework default is Native).
    const GUIWindowId guiId =
        realizeNativeDockPlacement(*env.coordinator, sourceDock, placementId, content, env.render,
                                   EWindowChromeMode::Hybrid);
    if (guiId == 0) {
        return result;
    }
    IGUIWindowSession* guiSession = env.coordinator->findSession(guiId);
    if (!guiSession || !guiSession->tree()) {
        env.coordinator->destroySession(guiId);
        return result;
    }

    const EditorWindowId editorId = nextFreeEditorWindowId(*env.windows, guiId);
    const bool bCreated = env.windows->find(editorId) == nullptr;
    EditorWindowSession* extra = bCreated ? env.windows->create(editorId) : env.windows->find(editorId);
    if (!extra) {
        env.coordinator->destroySession(guiId);
        return result;
    }
    extra->adoptHostTree(guiSession->tree(), guiId);

    const bool bOwnedTool = payload.scope == EEditorTabScope::EditorOwnedTool;
    std::shared_ptr<FDockContext> targetPtr =
        bOwnedTool ? extra->surface().ownedNestedDockPtr() : extra->surface().windowRootDockPtr();
    FDockContext* targetDock = targetPtr.get();
    if (!targetDock) {
        extra->adoptHostTree(nullptr);
        if (bCreated) {
            env.windows->destroy(editorId);
        }
        env.coordinator->destroySession(guiId);
        return result;
    }

    targetDock->bAllowDocking = true;
    targetDock->bAllowFloating = true;
    targetDock->bAllowTearOff = true;
    targetDock->sourceScope =
        bOwnedTool ? EDockSourceScope::EditorOwned : EDockSourceScope::WindowRoot;
    targetDock->hostWindowId = guiId;

    const EditorRootId targetRoot = bOwnedTool
                                        ? payload.ownerEditorId
                                        : (payload.ownerEditorId != kInvalidEditorRootId
                                               ? payload.ownerEditorId
                                               : extra->activeRoot().id());
    EditorDockWorkspace policyHost;
    policyHost.bind({
        .spawners        = env.spawners,
        .dock            = targetDock,
        .activeRootId    = targetRoot,
        .targetPlacement = bOwnedTool ? EEditorTabPlacement::EditorOwnedNested
                                      : EEditorTabPlacement::WindowRootDock,
        .windowId        = extra->windowId(),
    });
    hostEditorDockOnTree(*guiSession->tree(), targetPtr, guiSession->chrome().layout,
                         guiSession->nativeWindow());

    if (!sourceDock.transferNativePlacementTo(*targetDock, placementId)) {
        extra->adoptHostTree(nullptr);
        if (bCreated) {
            env.windows->destroy(editorId);
        }
        env.coordinator->destroySession(guiId);
        return result;
    }

    if (payload.ownerEditorId != kInvalidEditorRootId) {
        extra->root(payload.ownerEditorId).bindDocument(sourceSession.root(payload.ownerEditorId).document());
    }

    const FDockContext::FPanel* moved = targetDock->findPanelByStableKey(payload.tabId);
    result.editorWindowId = editorId;
    result.guiWindowId = guiId;
    result.targetPanelId = moved ? moved->id : kInvalidDockPanelId;
    (void)reclaimEditorWindowIfEmpty(env, sourceSession.windowId());
    return result;
}

bool handleDockNoTargetTearOff(FEditorNativeTearOff& env,
                               EditorWindowSession& sourceSession,
                               FDockContext& sourceDock,
                               DockPanelId panelId,
                               const glm::vec2& treeLocalPos,
                               const glm::vec2& size,
                               INativeWindow* sourceNative,
                               FEditorTearOffResult* out)
{
    if (out) {
        *out = {};
    }
    if (!env.coordinator || !env.windows) {
        return false;
    }
    const FDockContext::FPanel* panel = sourceDock.findPanel(panelId);
    const FDockPanelRecord* record = sourceDock.dockModel().findPanel(panelId);
    if (!panel || !record) {
        return false;
    }

    const FEditorTabDragPayload payload =
        payloadFromPanel(sourceDock, *panel, *record, sourceSession, env.spawners);
    if (!canTearOffEditorTab(payload)) {
        return true;
    }

    if (env.coordinator && env.coordinator->isHostOverlay(sourceSession.hostGuiWindowId())) {
        if (out) {
            out->editorWindowId = sourceSession.windowId();
            out->guiWindowId    = sourceSession.hostGuiWindowId();
            out->targetPanelId  = panelId;
        }
        return true;
    }

    if (editorWindowHasOnlyPanel(sourceSession, panelId) && sourceNative) {
        int wx = 0;
        int wy = 0;
        int ww = 0;
        int wh = 0;
        sourceNative->getWindowPosition(wx, wy);
        sourceNative->getWindowSize(ww, wh);
        glm::vec2 screen = treeLocalPos;
        screen.x += static_cast<float>(wx);
        screen.y += static_cast<float>(wy);
        const bool bInside =
            !sourceNative->isHidden() &&
            screen.x >= static_cast<float>(wx) &&
            screen.x < static_cast<float>(wx + ww) &&
            screen.y >= static_cast<float>(wy) &&
            screen.y < static_cast<float>(wy + wh);
        if (!bInside) {
            (void)sourceNative->show();
            (void)sourceNative->setWindowPosition(static_cast<int>(screen.x),
                                                  static_cast<int>(screen.y));
        }
        if (out) {
            out->editorWindowId = sourceSession.windowId();
            out->guiWindowId = sourceSession.hostGuiWindowId();
            out->targetPanelId = panelId;
        }
        return true;
    }

    FEditorTearOffGeometry geometry;
    geometry.pos = treeLocalPos;
    geometry.size = size;
    int screenX = 0;
    int screenY = 0;
    if (sourceNative && sourceNative->getWindowPosition(screenX, screenY)) {
        geometry.pos.x += static_cast<float>(screenX);
        geometry.pos.y += static_cast<float>(screenY);
        geometry.bScreenSpace = true;
    }

    const FEditorTearOffResult result =
        tearOffEditorPanelToNativeWindow(env, sourceSession, sourceDock, panelId, geometry);
    if (out) {
        *out = result;
    }
    return result.editorWindowId != kInvalidEditorWindowId;
}

} // namespace ya
