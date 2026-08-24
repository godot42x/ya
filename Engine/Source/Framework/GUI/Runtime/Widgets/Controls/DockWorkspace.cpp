#include "GUI/Widgets/Controls/DockWorkspace.h"

#include <algorithm>

namespace ya
{

DockPanelId UIDockWorkspace::addPanel(const std::string& name, std::shared_ptr<UIElement> widget)
{
    const DockPanelId id = _nextPanelId++;
    if (!_model.registerPanel({.id = id, .stableKey = name, .title = name}) ||
        !_model.addPanel(id)) {
        return kInvalidDockPanelId;
    }
    _panels.emplace(id, FPanel{id, name, std::move(widget)});
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
    for (FFloatingWindow& f : _floating) {
        if (f.id == id) {
            f.pos = pos;
            return;
        }
    }
}

} // namespace ya
