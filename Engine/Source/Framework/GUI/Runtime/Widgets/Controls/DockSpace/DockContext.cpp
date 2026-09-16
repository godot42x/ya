#include "GUI/Widgets/Controls/DockSpace/DockContext.h"

#include "GUI/Widgets/Controls/DockSpace/DockSpace.h"
#include "GUI/Widgets/UIElement.h"
#include "GUI/Widgets/WidgetTree.h"

#include <algorithm>
#include <optional>
#include <string>
#include <functional>
#include <unordered_set>

namespace ya
{

namespace
{

const char* projectionToJson(EDockFloatingProjection projection)
{
    return projection == EDockFloatingProjection::NativeWindow ? "nativeWindow" : "inProcessOverlay";
}

bool projectionFromJson(const nlohmann::json& entry, EDockFloatingProjection fallback, EDockFloatingProjection& out)
{
    if (!entry.contains("projection")) {
        out = fallback;
        return true;
    }
    if (!entry["projection"].is_string()) {
        return false;
    }
    const std::string value = entry["projection"].get<std::string>();
    if (value == "inProcessOverlay") {
        out = EDockFloatingProjection::InProcessOverlay;
        return true;
    }
    if (value == "nativeWindow") {
        out = EDockFloatingProjection::NativeWindow;
        return true;
    }
    return false;
}

const char* sourceScopeToJson(EDockSourceScope scope)
{
    return scope == EDockSourceScope::EditorOwned ? "editorOwned" : "windowRoot";
}

bool sourceScopeFromJson(const nlohmann::json& entry, EDockSourceScope fallback, EDockSourceScope& out)
{
    if (!entry.contains("sourceScope")) {
        out = fallback;
        return true;
    }
    if (!entry["sourceScope"].is_string()) {
        return false;
    }
    const std::string value = entry["sourceScope"].get<std::string>();
    if (value == "windowRoot") {
        out = EDockSourceScope::WindowRoot;
        return true;
    }
    if (value == "editorOwned") {
        out = EDockSourceScope::EditorOwned;
        return true;
    }
    return false;
}

const char* geometrySpaceToJson(EDockGeometrySpace space)
{
    return space == EDockGeometrySpace::Screen ? "screen" : "treeLocal";
}

bool geometrySpaceFromJson(const nlohmann::json& entry, EDockGeometrySpace& out)
{
    if (!entry.contains("geometrySpace")) {
        out = EDockGeometrySpace::TreeLocal;
        return true;
    }
    if (!entry["geometrySpace"].is_string()) {
        return false;
    }
    const std::string value = entry["geometrySpace"].get<std::string>();
    if (value == "treeLocal") {
        out = EDockGeometrySpace::TreeLocal;
        return true;
    }
    if (value == "screen") {
        out = EDockGeometrySpace::Screen;
        return true;
    }
    return false;
}

} // namespace

DockPanelId FDockContext::addPanel(const std::string& name, std::shared_ptr<UIElement> widget)
{
    return addPanel(name, name, std::move(widget));
}

DockPanelId FDockContext::addPanel(const std::string& stableKey, const std::string& title, std::shared_ptr<UIElement> widget)
{
    const DockPanelId id = _tabs.nextPanelId++;
    DockNodeId leafId = kInvalidDockNodeId;
    if (const DockNodeId preferred = adoptLeafFor(stableKey, 0, {});
        preferred != kInvalidDockNodeId) {
        leafId = preferred;
    }
    else if (const FDockNode* focused = _layout.findNode(_lastFocusedStackId);
             focused && focused->kind == EDockNodeKind::Stack &&
             focused->leafRole != EDockLeafRole::Page) {
        leafId = focused->id;
    }
    if (!_layout.registerPanel({.id = id, .stableKey = stableKey, .title = title}) ||
        !_layout.addPanel(id, leafId)) {
        return kInvalidDockPanelId;
    }
    _tabs.panels.emplace(id, FPanel{id, title, std::move(widget)});
    if (const FDockNode* leaf = _layout.findLeafForPanel(id)) {
        _lastFocusedStackId = leaf->id;
        if (_dockSpace) {
            _dockSpace->syncProjection(EDockProjectionSync::Stack, leaf->id);
        }
    }
    return id;
}

bool FDockContext::setPanelIdentity(DockPanelId id, uint32_t ownerEditorId, std::string documentKey)
{
    FPanel* panel = findPanel(id);
    if (!panel) {
        return false;
    }
    panel->ownerEditorId = ownerEditorId;
    panel->documentKey   = std::move(documentKey);
    return true;
}

bool FDockContext::setPanelIdentity(std::string_view stableKey, uint32_t ownerEditorId, std::string documentKey)
{
    const FPanel* panel = findPanelByStableKey(stableKey);
    return panel && setPanelIdentity(panel->id, ownerEditorId, std::move(documentKey));
}

bool FDockContext::setPanelClosable(DockPanelId id, bool closable)
{
    return _layout.setPanelClosable(id, closable);
}

bool FDockContext::setPanelClosable(std::string_view stableKey, bool closable)
{
    const FDockPanelRecord* record = _layout.findPanelByStableKey(std::string(stableKey));
    return record && _layout.setPanelClosable(record->id, closable);
}

bool FDockContext::closePanel(std::string_view stableKey)
{
    const FPanel* panel = findPanelByStableKey(stableKey);
    return panel && closePanel(panel->id);
}

bool FDockContext::closePanel(DockPanelId id)
{
    FPanel* panel = findPanel(id);
    if (!panel) {
        return false;
    }
    const FDockPanelRecord* record = _layout.findPanel(id);
    if (record && !record->closable) {
        return false;
    }
    if (panel->widget && panel->widget->isAttached()) {
        if (WidgetTree* tree = panel->widget->getTree()) {
            tree->detach(*panel->widget);
        }
    }
    if (isPanelFloating(id)) {
        endFloatingForPanel(id);
    }
    if (!_layout.removePanel(id)) {
        return false;
    }
    _tabs.panels.erase(id);
    fireDockUpdated();
    return true;
}

const FDockContext::FPanel* FDockContext::findPanel(DockPanelId id) const
{
    return _tabs.find(id);
}

FDockContext::FPanel* FDockContext::findPanel(DockPanelId id)
{
    return _tabs.find(id);
}

const FDockContext::FPanel* FDockContext::findPanelByStableKey(std::string_view stableKey) const
{
    const FDockPanelRecord* record = _layout.findPanelByStableKey(std::string(stableKey));
    return record ? findPanel(record->id) : nullptr;
}

bool FDockContext::hasPanel(std::string_view stableKey) const
{
    return findPanelByStableKey(stableKey) != nullptr;
}

bool FDockContext::activatePanel(std::string_view stableKey)
{
    const FPanel* panel = findPanelByStableKey(stableKey);
    if (!panel) {
        return false;
    }
    if (const FDockNode* leaf = _layout.findLeafForPanel(panel->id)) {
        rememberFocusedLeaf(leaf->id);
        if (leaf->selectedPanel == panel->id) {
            return true;
        }
        if (!_layout.selectPanel(panel->id)) {
            return false;
        }
        if (_dockSpace) {
            _dockSpace->syncProjection(EDockProjectionSync::Stack, leaf->id);
        }
        notifyDockLayoutListeners();
        return true;
    }
    const FFloatingWindow* floating = findFloatingByPanel(panel->id);
    if (!floating) {
        return false;
    }
    if (floating->activePanelId == panel->id) {
        return true;
    }
    setFloatingWindowActivePanel(floating->id, panel->id);
    fireFloatingUpdated();
    notifyDockLayoutListeners();
    return true;
}

std::vector<std::string> FDockContext::collectLayoutPanelKeys(const nlohmann::json& layout)
{
    std::vector<std::string> keys;
    std::unordered_set<std::string> seen;
    const auto add = [&](const std::string& key) {
        if (key.empty() || !seen.insert(key).second) {
            return;
        }
        keys.push_back(key);
    };
    std::function<void(const nlohmann::json&)> walk = [&](const nlohmann::json& node) {
        if (!node.is_object()) {
            return;
        }
        if (node.contains("panels") && node["panels"].is_array()) {
            for (const nlohmann::json& panel : node["panels"]) {
                if (panel.is_string()) {
                    add(panel.get<std::string>());
                }
            }
        }
        if (node.contains("root")) {
            walk(node["root"]);
        }
        if (node.contains("children") && node["children"].is_array()) {
            for (const nlohmann::json& child : node["children"]) {
                walk(child);
            }
        }
        if (node.contains("floating") && node["floating"].is_array()) {
            for (const nlohmann::json& window : node["floating"]) {
                walk(window);
            }
        }
        if (node.contains("windows") && node["windows"].is_array()) {
            for (const nlohmann::json& window : node["windows"]) {
                walk(window);
            }
        }
    };
    walk(layout);
    return keys;
}

void FDockContext::rememberFocusedStack(DockNodeId leafId)
{
    const FDockNode* leaf = _layout.findNode(leafId);
    if (!leaf || leaf->kind != EDockNodeKind::Stack) {
        return;
    }
    _lastFocusedStackId = leafId;
}

std::vector<std::string> FDockContext::panelStableKeys() const
{
    std::vector<std::string> keys;
    keys.reserve(_tabs.panels.size());
    for (const auto& [id, panel] : _tabs.panels) {
        (void)panel;
        if (const FDockPanelRecord* record = _layout.findPanel(id)) {
            keys.push_back(record->stableKey);
        }
    }
    return keys;
}

nlohmann::json FDockContext::sanitizeLayoutJson(nlohmann::json layout,
                                                const std::unordered_set<std::string>& knownKeys)
{
    const auto keepPanel = [&](const nlohmann::json& panel) {
        return panel.is_string() && knownKeys.contains(panel.get<std::string>());
    };
    const auto sanitizeNode = [&](auto& self, nlohmann::json& node) -> void {
        if (!node.is_object()) {
            return;
        }
        if (node.contains("panels") && node["panels"].is_array()) {
            nlohmann::json kept = nlohmann::json::array();
            for (const nlohmann::json& panel : node["panels"]) {
                if (keepPanel(panel)) {
                    kept.push_back(panel);
                }
            }
            node["panels"] = std::move(kept);
            if (node.contains("selected")) {
                const bool bSelectedKnown = node["selected"].is_string() &&
                                            knownKeys.contains(node["selected"].get<std::string>());
                if (!bSelectedKnown) {
                    if (!node["panels"].empty()) {
                        node["selected"] = node["panels"].front();
                    }
                    else {
                        node.erase("selected");
                    }
                }
            }
        }
        if (node.contains("children") && node["children"].is_array()) {
            for (nlohmann::json& child : node["children"]) {
                self(self, child);
            }
        }
    };

    if (layout.contains("root")) {
        sanitizeNode(sanitizeNode, layout["root"]);
    }
    if (layout.contains("floating") && layout["floating"].is_array()) {
        nlohmann::json keptWindows = nlohmann::json::array();
        for (nlohmann::json window : layout["floating"]) {
            sanitizeNode(sanitizeNode, window);
            if (window.contains("panels") && window["panels"].is_array() && !window["panels"].empty()) {
                keptWindows.push_back(std::move(window));
            }
        }
        layout["floating"] = std::move(keptWindows);
    }
    if (layout.contains("windows") && layout["windows"].is_array()) {
        nlohmann::json keptWindows = nlohmann::json::array();
        for (nlohmann::json window : layout["windows"]) {
            sanitizeNode(sanitizeNode, window);
            if (window.contains("panels") && window["panels"].is_array() && !window["panels"].empty()) {
                keptWindows.push_back(std::move(window));
            }
        }
        layout["windows"] = std::move(keptWindows);
    }
    return layout;
}

FDockFloatingWindowId FDockContext::tearOffPanel(DockPanelId panelId,
                                                 const glm::vec2& pos,
                                                 const glm::vec2& size,
                                                 EDockFloatingProjection projection)
{
    const FPanel* panel = findPanel(panelId);
    if (!panel) {
        return kInvalidFloatingWindowId;
    }
    // Already floating: keep it floating, just refresh geometry.
    for (FDockFloatingPlacement& existing : _floating) {
        if (std::find(existing.panelIds.begin(), existing.panelIds.end(), panelId) != existing.panelIds.end()) {
            existing.pos = pos;
            existing.size = size;
            return existing.id;
        }
    }
    // Detach from the dock tree (keeps the registry record).
    if (_layout.findLeafForPanel(panelId) && !_layout.detachFromTree(panelId)) {
        return kInvalidFloatingWindowId;
    }
    const FDockFloatingWindowId id = _nextFloatingWindowId++;
    FDockFloatingPlacement win;
    win.id             = id;
    win.panelIds       = {panelId};
    win.activePanelId  = panelId;
    win.pos            = pos;
    win.size           = size;
    win.projection     = projection;
    win.geometrySpace  = EDockGeometrySpace::TreeLocal;
    win.sourceScope    = sourceScope;
    win.targetWindowId = projection == EDockFloatingProjection::NativeWindow ? 0u : hostWindowId;
    win.ownerEditorId  = panel->ownerEditorId;
    win.documentKey    = panel->documentKey;
    _floating.push_back(std::move(win));
    return id;
}

bool FDockContext::addPanelToFloating(FDockFloatingWindowId targetId, DockPanelId panelId)
{
    FFloatingWindow* target = nullptr;
    for (FFloatingWindow& f : _floating) {
        if (f.id == targetId) {
            target = &f;
            break;
        }
    }
    if (!target || !findPanel(panelId) ||
        std::find(target->panelIds.begin(), target->panelIds.end(), panelId) != target->panelIds.end()) {
        return false;
    }
    // If the panel is already floating in its own window, remove it from there.
    // If it is docked, detach it from the dock tree first.
    if (_layout.findLeafForPanel(panelId) && !_layout.detachFromTree(panelId)) {
        return false;
    }
    endFloatingForPanel(panelId); // no-op unless floating in another window
    target->panelIds.push_back(panelId);
    target->activePanelId = panelId;
    fireFloatingUpdated();
    fireDockUpdated();
    return true;
}

bool FDockContext::dockPanelHome(DockPanelId panelId)
{
    if (!findPanel(panelId)) {
        return false;
    }
    const bool wasFloating = isPanelFloating(panelId);
    endFloatingForPanel(panelId);
    const bool ok = _layout.addPanel(panelId, _layout.getRootNode()->id);
    if (ok) {
        if (wasFloating) {
            fireDockUpdated();
        }
        fireFloatingUpdated();
    }
    return ok;
}

void FDockContext::endFloatingForPanel(DockPanelId panelId)
{
    bool bChanged = false;
    for (auto it = _floating.begin(); it != _floating.end();) {
        auto found = std::find(it->panelIds.begin(), it->panelIds.end(), panelId);
        if (found != it->panelIds.end()) {
            it->panelIds.erase(found);
            if (it->activePanelId == panelId) {
                it->activePanelId = it->panelIds.empty() ? kInvalidDockPanelId : it->panelIds.front();
            }
            bChanged = true;
        }
        if (it->panelIds.empty()) {
            it = _floating.erase(it);
        }
        else {
            ++it;
        }
    }
    if (bChanged) {
        fireFloatingUpdated();
    }
}

bool FDockContext::isPanelFloating(DockPanelId panelId) const
{
    return findFloatingByPanel(panelId) != nullptr;
}

const FDockContext::FFloatingWindow* FDockContext::findFloatingByPanel(DockPanelId panelId) const
{
    for (const FFloatingWindow& f : _floating) {
        if (std::find(f.panelIds.begin(), f.panelIds.end(), panelId) != f.panelIds.end()) {
            return &f;
        }
    }
    return nullptr;
}

const FDockContext::FFloatingWindow* FDockContext::findFloatingById(FDockFloatingWindowId id) const
{
    for (const FFloatingWindow& f : _floating) {
        if (f.id == id) {
            return &f;
        }
    }
    return nullptr;
}

void FDockContext::setFloatingWindowPos(FDockFloatingWindowId id, const glm::vec2& pos)
{
    if (FFloatingWindow* window = findFloatingByIdMutable(id)) {
        window->pos = pos;
    }
}

void FDockContext::setFloatingWindowRect(FDockFloatingWindowId id, const glm::vec2& pos, const glm::vec2& size)
{
    if (FFloatingWindow* window = findFloatingByIdMutable(id)) {
        window->pos  = pos;
        window->size = size;
    }
}

void FDockContext::setFloatingWindowActivePanel(FDockFloatingWindowId id, DockPanelId panelId)
{
    FFloatingWindow* window = findFloatingByIdMutable(id);
    if (!window) {
        return;
    }
    if (std::find(window->panelIds.begin(), window->panelIds.end(), panelId) == window->panelIds.end()) {
        return;
    }
    window->activePanelId = panelId;
}

void FDockContext::setFloatingHideTabBar(FDockFloatingWindowId id, bool hide)
{
    if (FFloatingWindow* window = findFloatingByIdMutable(id)) {
        window->bHideTabBar = hide;
    }
}

bool FDockContext::bindFloatingTargetWindow(FDockFloatingWindowId id, uint32_t windowId)
{
    FDockFloatingPlacement* placement = findFloatingByIdMutable(id);
    if (!placement || placement->projection != EDockFloatingProjection::NativeWindow || windowId == 0) {
        return false;
    }
    placement->targetWindowId = windowId;
    fireFloatingUpdated();
    return true;
}

std::optional<FDockContext::FDockExtractedPanel> FDockContext::extractPanel(DockPanelId id)
{
    FPanel* panel = findPanel(id);
    const FDockPanelRecord* record = _layout.findPanel(id);
    if (!panel || !record) {
        return std::nullopt;
    }

    FDockExtractedPanel extracted;
    extracted.stableKey     = record->stableKey;
    extracted.title         = record->title;
    extracted.closable      = record->closable;
    extracted.ownerEditorId = panel->ownerEditorId;
    extracted.documentKey   = panel->documentKey;
    extracted.widget        = panel->widget;

    if (extracted.widget && extracted.widget->isAttached()) {
        if (WidgetTree* tree = extracted.widget->getTree()) {
            tree->detach(*extracted.widget);
        }
    }
    if (isPanelFloating(id)) {
        endFloatingForPanel(id);
    }
    if (_layout.findLeafForPanel(id)) {
        (void)_layout.detachFromTree(id);
    }
    if (!_layout.removePanel(id)) {
        return std::nullopt;
    }
    _layout.pruneEmptyGenericLeaves();
    _tabs.panels.erase(id);
    fireDockUpdated();
    fireFloatingUpdated();
    return extracted;
}

DockPanelId FDockContext::adoptPanel(FDockExtractedPanel extracted)
{
    if (!extracted.widget || extracted.stableKey.empty() || hasPanel(extracted.stableKey)) {
        return kInvalidDockPanelId;
    }
    if (!acceptsImportedPanel(extracted.stableKey, extracted.ownerEditorId, extracted.documentKey)) {
        return kInvalidDockPanelId;
    }
    const DockPanelId id = _tabs.nextPanelId++;
    if (!_layout.registerPanel({.id       = id,
                               .stableKey = extracted.stableKey,
                               .title     = extracted.title,
                               .closable  = extracted.closable})) {
        return kInvalidDockPanelId;
    }
    _tabs.panels.emplace(id,
                    FPanel{id,
                           extracted.title,
                           std::move(extracted.widget),
                           extracted.ownerEditorId,
                           extracted.documentKey});
    return id;
}

DockPanelId FDockContext::transferPanelTo(FDockContext& target, DockPanelId panelId)
{
    if (&target == this) {
        return findPanel(panelId) ? panelId : kInvalidDockPanelId;
    }
    const FDockPanelRecord* record = _layout.findPanel(panelId);
    const FPanel* panel = findPanel(panelId);
    if (!record || !panel || target.hasPanel(record->stableKey)) {
        return kInvalidDockPanelId;
    }
    if (!target.acceptsImportedPanel(record->stableKey, panel->ownerEditorId, panel->documentKey)) {
        return kInvalidDockPanelId;
    }
    const std::string stableKey = record->stableKey;
    const uint32_t ownerEditorId = panel->ownerEditorId;
    const std::string documentKey = panel->documentKey;
    std::optional<FDockExtractedPanel> extracted = extractPanel(panelId);
    if (!extracted) {
        return kInvalidDockPanelId;
    }
    const DockPanelId newId = target.adoptPanel(std::move(*extracted));
    if (newId == kInvalidDockPanelId) {
        return kInvalidDockPanelId;
    }
    const DockNodeId leafId = target.adoptLeafFor(stableKey, ownerEditorId, documentKey);
    if (!target._layout.addPanel(newId, leafId)) {
        return kInvalidDockPanelId;
    }
    target.fireDockUpdated();
    return newId;
}

EDockDropCommit FDockContext::commitDrop(DockPanelId panelId, const FDockDropTarget& target)
{
    if (!findPanel(panelId) || !target.commitsDrop()) {
        return EDockDropCommit::Rejected;
    }

    const FDockNode* sourceStack = _layout.findStackForPanel(panelId);
    const bool       bWasFloating = isPanelFloating(panelId);
    if (!sourceStack && !bWasFloating && !findPanel(panelId)) {
        return EDockDropCommit::Rejected;
    }

    bool bChanged = false;
    switch (target.kind) {
    case EDockDropTargetKind::FloatingTabWell: {
        bChanged = addPanelToFloating(target.floatingWindowId, panelId);
        break;
    }
    case EDockDropTargetKind::TabWell: {
        const size_t insert = target.insertIndex;
        if (sourceStack) {
            bChanged = _layout.movePanel(panelId, target.stackId, insert, true);
        }
        else {
            bChanged = _layout.addPanel(panelId, target.stackId);
            if (bChanged) {
                (void)_layout.movePanel(panelId, target.stackId, insert, false);
            }
        }
        break;
    }
    case EDockDropTargetKind::TabStackCenter: {
        if (sourceStack && sourceStack->id == target.stackId) {
            return EDockDropCommit::Selected;
        }
        if (sourceStack) {
            bChanged = _layout.movePanel(panelId, target.stackId, SIZE_MAX, true);
        }
        else {
            bChanged = _layout.addPanel(panelId, target.stackId);
        }
        break;
    }
    case EDockDropTargetKind::TabStackSplit: {
        bChanged = _layout.splitStack(target.stackId, target.splitSide, panelId);
        break;
    }
    case EDockDropTargetKind::TabStackChooser:
    case EDockDropTargetKind::NoTarget: {
        return EDockDropCommit::Rejected;
    }
    }

    if (!bChanged) {
        return EDockDropCommit::Rejected;
    }
    if (bWasFloating && target.kind != EDockDropTargetKind::FloatingTabWell) {
        endFloatingForPanel(panelId);
    }
    _layout.pruneEmptyGenericLeaves();
    return EDockDropCommit::Applied;
}

bool FDockContext::transferNativePlacementTo(FDockContext& target, FDockFloatingWindowId placementId)
{
    const FDockFloatingPlacement* placement = findFloatingById(placementId);
    if (!placement || placement->projection != EDockFloatingProjection::NativeWindow) {
        return false;
    }
    const std::vector<DockPanelId> panelIds = placement->panelIds;
    if (panelIds.empty()) {
        return false;
    }
    for (const DockPanelId panelId : panelIds) {
        if (transferPanelTo(target, panelId) == kInvalidDockPanelId) {
            return false;
        }
    }
    return true;
}

bool FDockContext::setFloatingProjection(FDockFloatingWindowId id, EDockFloatingProjection projection)
{
    FDockFloatingPlacement* placement = findFloatingByIdMutable(id);
    if (!placement) {
        return false;
    }
    if (placement->projection == projection) {
        return true;
    }
    placement->projection = projection;
    if (projection == EDockFloatingProjection::NativeWindow) {
        placement->targetWindowId = 0;
    }
    else {
        placement->geometrySpace = EDockGeometrySpace::TreeLocal;
        if (placement->targetWindowId == 0) {
            placement->targetWindowId = hostWindowId;
        }
    }
    fireFloatingUpdated();
    return true;
}

bool FDockContext::setFloatingGeometrySpace(FDockFloatingWindowId id, EDockGeometrySpace space)
{
    FDockFloatingPlacement* placement = findFloatingByIdMutable(id);
    if (!placement) {
        return false;
    }
    if (placement->projection == EDockFloatingProjection::InProcessOverlay &&
        space != EDockGeometrySpace::TreeLocal) {
        return false;
    }
    placement->geometrySpace = space;
    notifyDockLayoutListeners();
    return true;
}

FDockContext::FFloatingWindow* FDockContext::findFloatingByIdMutable(FDockFloatingWindowId id)
{
    for (FFloatingWindow& window : _floating) {
        if (window.id == id) {
            return &window;
        }
    }
    return nullptr;
}

nlohmann::json FDockContext::exportLayoutJson() const
{
    nlohmann::json layout = _layout.exportLayoutJson();
    nlohmann::json overlayWindows = nlohmann::json::array();
    nlohmann::json nativeWindows  = nlohmann::json::array();
    for (const FFloatingWindow& window : _floating) {
        nlohmann::json panels = nlohmann::json::array();
        for (const DockPanelId panelId : window.panelIds) {
            const FDockPanelRecord* record = _layout.findPanel(panelId);
            if (!record) {
                continue;
            }
            panels.push_back(record->stableKey);
        }
        if (panels.empty()) {
            continue;
        }
        nlohmann::json entry = nlohmann::json::object();
        entry["panels"] = std::move(panels);
        if (const FDockPanelRecord* selected = _layout.findPanel(window.activePanelId)) {
            entry["selected"] = selected->stableKey;
        }
        entry["pos"]  = nlohmann::json::array({window.pos.x, window.pos.y});
        entry["size"] = nlohmann::json::array({window.size.x, window.size.y});
        entry["projection"] = projectionToJson(window.projection);
        const EDockGeometrySpace space =
            window.projection == EDockFloatingProjection::InProcessOverlay
                ? EDockGeometrySpace::TreeLocal
                : window.geometrySpace;
        entry["geometrySpace"] = geometrySpaceToJson(space);
        entry["sourceScope"] = sourceScopeToJson(window.sourceScope);
        entry["targetWindowId"] = window.targetWindowId;
        entry["ownerEditorId"] = window.ownerEditorId;
        if (!window.documentKey.empty()) {
            entry["documentKey"] = window.documentKey;
        }
        if (window.bHideTabBar) {
            entry["hideTabBar"] = true;
        }
        if (window.projection == EDockFloatingProjection::NativeWindow) {
            nativeWindows.push_back(std::move(entry));
        }
        else {
            overlayWindows.push_back(std::move(entry));
        }
    }
    layout["floating"] = std::move(overlayWindows);
    layout["windows"]  = std::move(nativeWindows);
    return layout;
}

bool FDockContext::importLayoutJson(const nlohmann::json& layout)
{
    struct FPendingFloating
    {
        std::vector<DockPanelId> panelIds;
        DockPanelId              activePanelId = kInvalidDockPanelId;
        glm::vec2                pos{180.0f, 140.0f};
        glm::vec2                size{320.0f, 240.0f};
        bool                     bHideTabBar = false;
        EDockFloatingProjection  projection = EDockFloatingProjection::InProcessOverlay;
        EDockGeometrySpace       geometrySpace = EDockGeometrySpace::TreeLocal;
        EDockSourceScope         sourceScope = EDockSourceScope::WindowRoot;
        uint32_t                 targetWindowId = 0;
        uint32_t                 ownerEditorId = 0;
        std::string              documentKey;
    };

    std::vector<FPendingFloating> pending;
    std::unordered_set<DockPanelId> seenPanels;
    const auto parseArray = [&](const char* field, EDockFloatingProjection fallback) -> bool {
        if (!layout.contains(field)) {
            return true;
        }
        if (!layout[field].is_array()) {
            return false;
        }
        for (const nlohmann::json& entry : layout[field]) {
            if (!entry.is_object() || !entry.contains("panels") || !entry["panels"].is_array() ||
                entry["panels"].empty()) {
                return false;
            }
            FPendingFloating window;
            for (const nlohmann::json& panelKeyJson : entry["panels"]) {
                if (!panelKeyJson.is_string()) {
                    return false;
                }
                const FDockPanelRecord* record = _layout.findPanelByStableKey(panelKeyJson.get<std::string>());
                if (!record) {
                    return false;
                }
                if (!seenPanels.insert(record->id).second) {
                    return false;
                }
                window.panelIds.push_back(record->id);
            }
            if (entry.contains("selected")) {
                if (!entry["selected"].is_string()) {
                    return false;
                }
                const FDockPanelRecord* selected = _layout.findPanelByStableKey(entry["selected"].get<std::string>());
                if (!selected) {
                    return false;
                }
                window.activePanelId = selected->id;
            }
            else {
                window.activePanelId = window.panelIds.front();
            }
            if (entry.contains("pos") && entry["pos"].is_array() && entry["pos"].size() == 2) {
                window.pos = {entry["pos"][0].get<float>(), entry["pos"][1].get<float>()};
            }
            if (entry.contains("size") && entry["size"].is_array() && entry["size"].size() == 2) {
                window.size = {entry["size"][0].get<float>(), entry["size"][1].get<float>()};
            }
            window.bHideTabBar = entry.value("hideTabBar", false);
            if (!projectionFromJson(entry, fallback, window.projection) ||
                !geometrySpaceFromJson(entry, window.geometrySpace) ||
                !sourceScopeFromJson(entry, sourceScope, window.sourceScope)) {
                return false;
            }
            if (window.projection == EDockFloatingProjection::InProcessOverlay) {
                window.geometrySpace = EDockGeometrySpace::TreeLocal;
            }
            if (entry.contains("targetWindowId")) {
                if (!entry["targetWindowId"].is_number()) {
                    return false;
                }
                window.targetWindowId = entry["targetWindowId"].get<uint32_t>();
            }
            else if (window.projection == EDockFloatingProjection::NativeWindow) {
                window.targetWindowId = 0;
            }
            else {
                window.targetWindowId = hostWindowId;
            }
            if (entry.contains("ownerEditorId")) {
                if (!entry["ownerEditorId"].is_number()) {
                    return false;
                }
                window.ownerEditorId = entry["ownerEditorId"].get<uint32_t>();
            }
            if (entry.contains("documentKey")) {
                if (!entry["documentKey"].is_string()) {
                    return false;
                }
                window.documentKey = entry["documentKey"].get<std::string>();
            }
            pending.push_back(std::move(window));
        }
        return true;
    };
    if (!parseArray("floating", EDockFloatingProjection::InProcessOverlay) ||
        !parseArray("windows", EDockFloatingProjection::NativeWindow)) {
        return false;
    }

    const std::vector<FFloatingWindow> previousFloating = _floating;
    const FDockFloatingWindowId previousNextId = _nextFloatingWindowId;
    _floating.clear();
    if (!_layout.importLayoutJson(layout)) {
        _floating = previousFloating;
        _nextFloatingWindowId = previousNextId;
        return false;
    }

    for (const FPendingFloating& window : pending) {
        FDockFloatingWindowId floatingId = kInvalidFloatingWindowId;
        for (size_t index = 0; index < window.panelIds.size(); ++index) {
            const DockPanelId panelId = window.panelIds[index];
            if (index == 0) {
                floatingId = tearOffPanel(panelId, window.pos, window.size, window.projection);
                if (floatingId == kInvalidFloatingWindowId) {
                    _floating = previousFloating;
                    _nextFloatingWindowId = previousNextId;
                    return false;
                }
            }
            else if (!addPanelToFloating(floatingId, panelId)) {
                _floating = previousFloating;
                _nextFloatingWindowId = previousNextId;
                return false;
            }
        }
        setFloatingWindowActivePanel(floatingId, window.activePanelId);
        setFloatingHideTabBar(floatingId, window.bHideTabBar);
        if (FDockFloatingPlacement* placement = findFloatingByIdMutable(floatingId)) {
            placement->projection     = window.projection;
            placement->geometrySpace  = window.geometrySpace;
            placement->sourceScope    = window.sourceScope;
            placement->targetWindowId = window.targetWindowId;
            placement->ownerEditorId  = window.ownerEditorId;
            placement->documentKey    = window.documentKey;
        }
    }

    fireFloatingUpdated();
    return true;
}

} // namespace ya
