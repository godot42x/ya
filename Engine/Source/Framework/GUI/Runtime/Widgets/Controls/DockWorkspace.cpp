#include "GUI/Widgets/Controls/DockWorkspace.h"

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

} // namespace ya
