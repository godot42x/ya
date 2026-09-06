#include "GUI/Widgets/Controls/DockSpace/DockContext.h"

#include "GUI/Widgets/UIElement.h"
#include "GUI/Widgets/WidgetTree.h"

#include <algorithm>
#include <string>
#include <functional>
#include <unordered_set>

namespace ya
{

DockPanelId FDockContext::addPanel(const std::string& name, std::shared_ptr<UIElement> widget)
{
    return addPanel(name, name, std::move(widget));
}

DockPanelId FDockContext::addPanel(const std::string& stableKey, const std::string& title, std::shared_ptr<UIElement> widget)
{
    const DockPanelId id = _nextPanelId++;
    DockNodeId leafId = kInvalidDockNodeId;
    if (const FDockNode* focused = _model.findNode(_lastFocusedLeafId);
        focused && focused->kind == EDockNodeKind::Leaf) {
        leafId = focused->id;
    }
    if (!_model.registerPanel({.id = id, .stableKey = stableKey, .title = title}) ||
        !_model.addPanel(id, leafId)) {
        return kInvalidDockPanelId;
    }
    _panels.emplace(id, FPanel{id, title, std::move(widget)});
    if (const FDockNode* leaf = _model.findLeafForPanel(id)) {
        _lastFocusedLeafId = leaf->id;
    }
    return id;
}

bool FDockContext::setPanelClosable(DockPanelId id, bool closable)
{
    return _model.setPanelClosable(id, closable);
}

bool FDockContext::setPanelClosable(std::string_view stableKey, bool closable)
{
    const FDockPanelRecord* record = _model.findPanelByStableKey(std::string(stableKey));
    return record && _model.setPanelClosable(record->id, closable);
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
    const FDockPanelRecord* record = _model.findPanel(id);
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
    if (!_model.removePanel(id)) {
        return false;
    }
    _panels.erase(id);
    fireDockUpdated();
    return true;
}

const FDockContext::FPanel* FDockContext::findPanel(DockPanelId id) const
{
    auto it = _panels.find(id);
    return it == _panels.end() ? nullptr : &it->second;
}

FDockContext::FPanel* FDockContext::findPanel(DockPanelId id)
{
    auto it = _panels.find(id);
    return it == _panels.end() ? nullptr : &it->second;
}

const FDockContext::FPanel* FDockContext::findPanelByStableKey(std::string_view stableKey) const
{
    const FDockPanelRecord* record = _model.findPanelByStableKey(std::string(stableKey));
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
    if (const FDockNode* leaf = _model.findLeafForPanel(panel->id)) {
        rememberFocusedLeaf(leaf->id);
        if (leaf->selectedPanel == panel->id) {
            return true;
        }
        if (!_model.selectPanel(panel->id)) {
            return false;
        }
        fireDockUpdated();
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
    };
    walk(layout);
    return keys;
}

void FDockContext::rememberFocusedLeaf(DockNodeId leafId)
{
    const FDockNode* leaf = _model.findNode(leafId);
    if (!leaf || leaf->kind != EDockNodeKind::Leaf) {
        return;
    }
    _lastFocusedLeafId = leafId;
}

std::vector<std::string> FDockContext::panelStableKeys() const
{
    std::vector<std::string> keys;
    keys.reserve(_panels.size());
    for (const auto& [id, panel] : _panels) {
        (void)panel;
        if (const FDockPanelRecord* record = _model.findPanel(id)) {
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
    return layout;
}

FDockFloatingWindowId FDockContext::tearOffPanel(DockPanelId panelId, const glm::vec2& pos, const glm::vec2& size)
{
    if (!findPanel(panelId)) {
        return kInvalidFloatingWindowId;
    }
    // Already floating: keep it floating, just refresh geometry.
    for (FFloatingWindow& existing : _floating) {
        if (std::find(existing.panelIds.begin(), existing.panelIds.end(), panelId) != existing.panelIds.end()) {
            existing.pos = pos;
            existing.size = size;
            return existing.id;
        }
    }
    // Detach from the dock tree (keeps the registry record).
    if (_model.findLeafForPanel(panelId) && !_model.detachFromTree(panelId)) {
        return kInvalidFloatingWindowId;
    }
    const FDockFloatingWindowId id = _nextFloatingWindowId++;
    FFloatingWindow win;
    win.id = id;
    win.panelIds = {panelId};
    win.activePanelId = panelId;
    win.pos = pos;
    win.size = size;
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
    if (_model.findLeafForPanel(panelId) && !_model.detachFromTree(panelId)) {
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
    const bool ok = _model.addPanel(panelId, _model.getRootNode()->id);
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
    nlohmann::json layout = _model.exportLayoutJson();
    nlohmann::json floating = nlohmann::json::array();
    for (const FFloatingWindow& window : _floating) {
        nlohmann::json panels = nlohmann::json::array();
        for (const DockPanelId panelId : window.panelIds) {
            const FDockPanelRecord* record = _model.findPanel(panelId);
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
        if (const FDockPanelRecord* selected = _model.findPanel(window.activePanelId)) {
            entry["selected"] = selected->stableKey;
        }
        entry["pos"]  = nlohmann::json::array({window.pos.x, window.pos.y});
        entry["size"] = nlohmann::json::array({window.size.x, window.size.y});
        if (window.bHideTabBar) {
            entry["hideTabBar"] = true;
        }
        floating.push_back(std::move(entry));
    }
    layout["floating"] = std::move(floating);
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
    };

    std::vector<FPendingFloating> pending;
    if (layout.contains("floating")) {
        if (!layout["floating"].is_array()) {
            return false;
        }
        for (const nlohmann::json& entry : layout["floating"]) {
            if (!entry.is_object() || !entry.contains("panels") || !entry["panels"].is_array() || entry["panels"].empty()) {
                return false;
            }
            FPendingFloating window;
            for (const nlohmann::json& panelKeyJson : entry["panels"]) {
                if (!panelKeyJson.is_string()) {
                    return false;
                }
                const FDockPanelRecord* record = _model.findPanelByStableKey(panelKeyJson.get<std::string>());
                if (!record) {
                    return false;
                }
                window.panelIds.push_back(record->id);
            }
            if (entry.contains("selected")) {
                if (!entry["selected"].is_string()) {
                    return false;
                }
                const FDockPanelRecord* selected = _model.findPanelByStableKey(entry["selected"].get<std::string>());
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
            pending.push_back(std::move(window));
        }
    }

    const std::vector<FFloatingWindow> previousFloating = _floating;
    const FDockFloatingWindowId previousNextId = _nextFloatingWindowId;
    _floating.clear();
    if (!_model.importLayoutJson(layout)) {
        _floating = previousFloating;
        _nextFloatingWindowId = previousNextId;
        return false;
    }

    for (const FPendingFloating& window : pending) {
        FDockFloatingWindowId floatingId = kInvalidFloatingWindowId;
        for (size_t index = 0; index < window.panelIds.size(); ++index) {
            const DockPanelId panelId = window.panelIds[index];
            if (index == 0) {
                floatingId = tearOffPanel(panelId, window.pos, window.size);
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
    }

    fireFloatingUpdated();
    return true;
}

} // namespace ya
