#include "GUI/Widgets/Controls/DockWorkspace.h"

#include <algorithm>

namespace ya
{

DockPanelId UIDockWorkspace::addPanel(const std::string& name, std::shared_ptr<UIElement> widget)
{
    return addPanel(name, name, std::move(widget));
}

DockPanelId UIDockWorkspace::addPanel(const std::string& stableKey, const std::string& title, std::shared_ptr<UIElement> widget)
{
    const DockPanelId id = _nextPanelId++;
    if (!_model.registerPanel({.id = id, .stableKey = stableKey, .title = title}) ||
        !_model.addPanel(id)) {
        return kInvalidDockPanelId;
    }
    _panels.emplace(id, FPanel{id, title, std::move(widget)});
    return id;
}

const UIDockWorkspace::FPanel* UIDockWorkspace::findPanel(DockPanelId id) const
{
    auto it = _panels.find(id);
    return it == _panels.end() ? nullptr : &it->second;
}

UIDockWorkspace::FPanel* UIDockWorkspace::findPanel(DockPanelId id)
{
    auto it = _panels.find(id);
    return it == _panels.end() ? nullptr : &it->second;
}

FDockFloatingWindowId UIDockWorkspace::tearOffPanel(DockPanelId panelId, const glm::vec2& pos, const glm::vec2& size)
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

bool UIDockWorkspace::addPanelToFloating(FDockFloatingWindowId targetId, DockPanelId panelId)
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

bool UIDockWorkspace::dockPanelHome(DockPanelId panelId)
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

void UIDockWorkspace::endFloatingForPanel(DockPanelId panelId)
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

bool UIDockWorkspace::isPanelFloating(DockPanelId panelId) const
{
    return findFloatingByPanel(panelId) != nullptr;
}

const UIDockWorkspace::FFloatingWindow* UIDockWorkspace::findFloatingByPanel(DockPanelId panelId) const
{
    for (const FFloatingWindow& f : _floating) {
        if (std::find(f.panelIds.begin(), f.panelIds.end(), panelId) != f.panelIds.end()) {
            return &f;
        }
    }
    return nullptr;
}

const UIDockWorkspace::FFloatingWindow* UIDockWorkspace::findFloatingById(FDockFloatingWindowId id) const
{
    for (const FFloatingWindow& f : _floating) {
        if (f.id == id) {
            return &f;
        }
    }
    return nullptr;
}

void UIDockWorkspace::setFloatingWindowPos(FDockFloatingWindowId id, const glm::vec2& pos)
{
    if (FFloatingWindow* window = findFloatingByIdMutable(id)) {
        window->pos = pos;
    }
}

void UIDockWorkspace::setFloatingWindowRect(FDockFloatingWindowId id, const glm::vec2& pos, const glm::vec2& size)
{
    if (FFloatingWindow* window = findFloatingByIdMutable(id)) {
        window->pos  = pos;
        window->size = size;
    }
}

void UIDockWorkspace::setFloatingWindowActivePanel(FDockFloatingWindowId id, DockPanelId panelId)
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

void UIDockWorkspace::setFloatingHideTabBar(FDockFloatingWindowId id, bool hide)
{
    if (FFloatingWindow* window = findFloatingByIdMutable(id)) {
        window->bHideTabBar = hide;
    }
}

UIDockWorkspace::FFloatingWindow* UIDockWorkspace::findFloatingByIdMutable(FDockFloatingWindowId id)
{
    for (FFloatingWindow& window : _floating) {
        if (window.id == id) {
            return &window;
        }
    }
    return nullptr;
}

nlohmann::json UIDockWorkspace::exportLayoutJson() const
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

bool UIDockWorkspace::importLayoutJson(const nlohmann::json& layout)
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
