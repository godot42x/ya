#pragma once

#include "GUI/Widgets/Controls/DockNode.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace ya
{

struct UIElement;

/// Central dock workspace (UE-style registration / policy switches). Holds the
/// shared dock tree, the panel registry and the switches that every DockSpace
/// and floating host reads. In the future one workspace can back multiple
/// window projections (the seed of the coordinator behind multi-window dock).
struct YA_GUI_API UIDockWorkspace
{
    struct FPanel
    {
        DockPanelId                id = kInvalidDockPanelId;
        std::string                name;
        std::shared_ptr<UIElement> widget;
    };

    /// Dock policy switches (central on/off for the whole workspace).
    bool bAllowDocking  = true;   // panels may dock into DockSpaces
    bool bAllowFloating = false;  // floating windows may exist
    bool bAllowTearOff  = false;  // drag-out may create a floating window

    /// Register a panel and dock it into the dock tree's root leaf. Returns the
    /// stable panel id (kInvalidDockPanelId on failure).
    DockPanelId addPanel(const std::string& name, std::shared_ptr<UIElement> widget);

    [[nodiscard]] const FPanel* findPanel(DockPanelId id) const;
    [[nodiscard]] FPanel* findPanel(DockPanelId id);
    [[nodiscard]] FDockTreeModel& dockModel() { return _model; }
    [[nodiscard]] const FDockTreeModel& dockModel() const { return _model; }

private:
    FDockTreeModel _model;
    std::unordered_map<DockPanelId, FPanel> _panels;
    DockPanelId _nextPanelId = 1;
};

} // namespace ya
