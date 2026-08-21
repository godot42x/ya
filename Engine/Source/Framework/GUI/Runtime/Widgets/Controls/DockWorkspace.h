#pragma once

#include "GUI/Widgets/Controls/DockNode.h"

#include <glm/glm.hpp>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ya
{

struct UIElement;
struct UIDockFloatingHost;
using FDockFloatingWindowId = uint64_t;
inline constexpr FDockFloatingWindowId kInvalidFloatingWindowId = 0;

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

    struct FFloatingWindow
    {
        FDockFloatingWindowId id    = kInvalidFloatingWindowId;
        DockPanelId           panelId = kInvalidDockPanelId;
        glm::vec2             pos    {0.0f, 0.0f};
        glm::vec2             size   {320.0f, 240.0f};
    };

    /// Dock policy switches (central on/off for the whole workspace).
    bool bAllowDocking  = true;   // panels may dock into DockSpaces
    bool bAllowFloating = false;  // floating windows may exist
    bool bAllowTearOff  = false;  // drag-out may create a floating window

    /// Register a panel and dock it into the dock tree's root leaf. Returns the
    /// stable panel id (kInvalidDockPanelId on failure).
    DockPanelId addPanel(const std::string& name, std::shared_ptr<UIElement> widget);

    // === Floating (Phase 5) ===
    /// Bind the floating host that presents this workspace's floating windows.
    void setFloatingHost(UIDockFloatingHost* host) { _floatingHost = host; }
    [[nodiscard]] UIDockFloatingHost* floatingHost() const { return _floatingHost; }

    /// Tear a panel out of the dock tree into a floating window (if allowed).
    /// Returns the floating window id (kInvalidFloatingWindowId on failure).
    FDockFloatingWindowId tearOffPanel(DockPanelId panelId, const glm::vec2& pos, const glm::vec2& size);
    /// Re-dock a floating panel back to the dock tree's root leaf.
    bool dockPanelHome(DockPanelId panelId);
    /// End the floating window for a panel (no-op if not floating). Does not
    /// re-dock the panel; leaves it detached in the registry.
    void endFloatingForPanel(DockPanelId panelId);
    [[nodiscard]] bool isPanelFloating(DockPanelId panelId) const;
    [[nodiscard]] const FFloatingWindow* findFloatingByPanel(DockPanelId panelId) const;
    [[nodiscard]] const std::vector<FFloatingWindow>& floatingWindows() const { return _floating; }

    /// The DockSpace re-projects its tree when panels move into/out of dock.
    void setOnDockUpdated(std::function<void()> cb) { _onDockUpdated = std::move(cb); }
    /// The floating host re-syncs its window set when floating changes.
    void setOnFloatingUpdated(std::function<void()> cb) { _onFloatingUpdated = std::move(cb); }
    void fireDockUpdated() { if (_onDockUpdated) _onDockUpdated(); }
    void fireFloatingUpdated() { if (_onFloatingUpdated) _onFloatingUpdated(); }

    [[nodiscard]] const FPanel* findPanel(DockPanelId id) const;
    [[nodiscard]] FPanel* findPanel(DockPanelId id);
    [[nodiscard]] FDockTreeModel& dockModel() { return _model; }
    [[nodiscard]] const FDockTreeModel& dockModel() const { return _model; }

private:
    FDockTreeModel _model;
    std::unordered_map<DockPanelId, FPanel> _panels;
    std::vector<FFloatingWindow> _floating;
    DockPanelId _nextPanelId = 1;
    FDockFloatingWindowId _nextFloatingWindowId = 1;
    UIDockFloatingHost* _floatingHost = nullptr;
    std::function<void()> _onDockUpdated;
    std::function<void()> _onFloatingUpdated;
};

} // namespace ya
