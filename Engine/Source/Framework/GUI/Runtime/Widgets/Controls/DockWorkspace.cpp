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
        if (existing.panelId == panelId) {
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
    _floating.push_back(FFloatingWindow{id, panelId, pos, size});
    return id;
}

bool UIDockWorkspace::dockPanelHome(DockPanelId panelId)
{
    if (!findPanel(panelId)) {
        return false;
    }
    endFloatingForPanel(panelId);
    const bool ok = _model.addPanel(panelId, _model.root()->id);
    if (ok) {
        fireDockUpdated();
        fireFloatingUpdated();
    }
    return ok;
}

void UIDockWorkspace::endFloatingForPanel(DockPanelId panelId)
{
    const size_t before = _floating.size();
    _floating.erase(std::remove_if(_floating.begin(), _floating.end(),
                                   [panelId](const FFloatingWindow& f) { return f.panelId == panelId; }),
                    _floating.end());
    if (_floating.size() != before) {
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
        if (f.panelId == panelId) return &f;
    }
    return nullptr;
}

} // namespace ya
