#pragma once

#include "GUI/Widgets/Controls/DockSpace/DockNode.h"

#include <glm/glm.hpp>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ya
{

struct UIElement;
struct UIDockFloatingHost;
struct UIDockSpace;
using FDockFloatingWindowId = uint64_t;
inline constexpr FDockFloatingWindowId kInvalidFloatingWindowId = 0;

/// Shared dock session. Not a widget.
///
/// Owns the panel registry, `FDockTreeModel`, floating-window records, policy
/// flags, and layout JSON. The two projections that read this session are:
///   - `UIDockSpace` — in-window nested splits + tab groups
///   - `UIDockFloatingHost` — Popup-layer torn-off windows
/// One context can back both projections (and later multiple windows).
struct YA_GUI_API FDockContext
{
    struct FPanel
    {
        DockPanelId                id = kInvalidDockPanelId;
        std::string                name;
        std::shared_ptr<UIElement> widget;
    };

    struct FFloatingWindow
    {
        FDockFloatingWindowId      id         = kInvalidFloatingWindowId;
        std::vector<DockPanelId>   panelIds;      ///< All tabs hosted by this floating window.
        DockPanelId                activePanelId = kInvalidDockPanelId; ///< Currently visible tab.
        glm::vec2                  pos  {0.0f, 0.0f};
        glm::vec2                  size {320.0f, 240.0f};
        bool                       bHideTabBar = false;
    };

    /// Dock policy switches (central on/off for the whole session).
    bool bAllowDocking  = true;   // panels may dock into DockSpaces
    bool bAllowFloating = false;  // floating windows may exist
    bool bAllowTearOff  = false;  // drag-out may create a floating window

    /// Register a panel and dock it into the dock tree's root leaf. Returns the
    /// stable panel id (kInvalidDockPanelId on failure).
    DockPanelId addPanel(const std::string& name, std::shared_ptr<UIElement> widget);
    DockPanelId addPanel(const std::string& stableKey, const std::string& title, std::shared_ptr<UIElement> widget);

    // === Floating (Phase 5) ===
    /// Bind the floating host that presents this context's floating windows.
    void setFloatingHost(UIDockFloatingHost* host) { _floatingHost = host; }
    [[nodiscard]] UIDockFloatingHost* floatingHost() const { return _floatingHost; }

    /// Bind the DockSpace that projects this context's dock tree. Used by
    /// floating-window tab drags to drive the dock drop-preview (chooser),
    /// since the DockSpace lives on a different layer than the floating host.
    void setDockSpace(UIDockSpace* space) { _dockSpace = space; }
    [[nodiscard]] UIDockSpace* dockSpace() const { return _dockSpace; }

    /// Tear a panel out of the dock tree into a floating window (if allowed).
    /// Returns the floating window id (kInvalidFloatingWindowId on failure).
    FDockFloatingWindowId tearOffPanel(DockPanelId panelId, const glm::vec2& pos, const glm::vec2& size);
    /// Add an existing panel as a new tab into an existing floating window,
    /// detaching it from the dock tree first (no-op if the panel is the source
    /// window's active tab). Returns true on success.
    bool addPanelToFloating(FDockFloatingWindowId targetId, DockPanelId panelId);
    /// Re-dock a floating panel back to the dock tree's root leaf.
    bool dockPanelHome(DockPanelId panelId);
    /// End the floating window for a panel (no-op if not floating). Does not
    /// re-dock the panel; leaves it detached in the registry.
    void endFloatingForPanel(DockPanelId panelId);
    [[nodiscard]] bool isPanelFloating(DockPanelId panelId) const;
    [[nodiscard]] const FFloatingWindow* findFloatingByPanel(DockPanelId panelId) const;
    [[nodiscard]] const FFloatingWindow* findFloatingById(FDockFloatingWindowId id) const;
    [[nodiscard]] const std::vector<FFloatingWindow>& floatingWindows() const { return _floating; }
    /// Update a floating window's on-screen position (called as the window moves).
    void setFloatingWindowPos(FDockFloatingWindowId id, const glm::vec2& pos);
    /// Update a floating window's logical position and size without notifying listeners.
    void setFloatingWindowRect(FDockFloatingWindowId id, const glm::vec2& pos, const glm::vec2& size);
    void setFloatingWindowActivePanel(FDockFloatingWindowId id, DockPanelId panelId);
    void setFloatingHideTabBar(FDockFloatingWindowId id, bool hide);

    /// Serialize the docked tree plus floating windows (stable panel keys + geometry).
    [[nodiscard]] nlohmann::json exportLayoutJson() const;
    /// Restore docked tree then floating windows. Missing `floating` is treated as empty
    /// (backward compatible with tree-only snapshots). Unknown panel keys fail the import.
    bool importLayoutJson(const nlohmann::json& layout);

    /// The DockSpace re-projects its tree when the dock *structure* changes
    /// (drop / tear-off / import). Split-ratio, tab selection, and hide-tab-bar
    /// must not go through this: they update live chrome and call
    /// notifyDockLayoutListeners() so persist runs without rematerializing.
    void setOnDockUpdated(std::function<void()> cb) { _onDockUpdated = std::move(cb); }
    void appendOnDockUpdated(std::function<void()> cb) { _onDockUpdatedListeners.push_back(std::move(cb)); }
    /// The floating host re-syncs its window set when floating changes.
    void setOnFloatingUpdated(std::function<void()> cb) { _onFloatingUpdated = std::move(cb); }
    void appendOnFloatingUpdated(std::function<void()> cb) { _onFloatingUpdatedListeners.push_back(std::move(cb)); }
    /// Persist listeners only (layout JSON). Does not rematerialize DockSpace.
    void notifyDockLayoutListeners()
    {
        for (const std::function<void()>& listener : _onDockUpdatedListeners) {
            if (listener) {
                listener();
            }
        }
    }
    void fireDockUpdated()
    {
        if (_onDockUpdated) {
            _onDockUpdated();
        }
        notifyDockLayoutListeners();
    }
    void fireFloatingUpdated()
    {
        if (_onFloatingUpdated) {
            _onFloatingUpdated();
        }
        for (const std::function<void()>& listener : _onFloatingUpdatedListeners) {
            if (listener) {
                listener();
            }
        }
    }

    [[nodiscard]] const FPanel* findPanel(DockPanelId id) const;
    [[nodiscard]] FPanel* findPanel(DockPanelId id);
    [[nodiscard]] const FPanel* findPanelByStableKey(std::string_view stableKey) const;
    [[nodiscard]] bool hasPanel(std::string_view stableKey) const;
    /// Select a docked or floating panel by stable key. Rematerializes the
    /// owning projection so the panel widget is grafted as the visible tab.
    bool activatePanel(std::string_view stableKey);
    /// Collect unique panel keys from a layout JSON (docked tree + floating).
    [[nodiscard]] static std::vector<std::string> collectLayoutPanelKeys(const nlohmann::json& layout);
    bool setPanelClosable(DockPanelId id, bool closable);
    bool setPanelClosable(std::string_view stableKey, bool closable);
    /// Close a closable panel: unlink it from the dock tree or floating window
    /// and drop the registry record. Non-closable panels return false.
    bool closePanel(DockPanelId id);
    [[nodiscard]] FDockTreeModel& dockModel() { return _model; }
    [[nodiscard]] const FDockTreeModel& dockModel() const { return _model; }

private:
    FDockTreeModel _model;
    std::unordered_map<DockPanelId, FPanel> _panels;
    std::vector<FFloatingWindow> _floating;
    DockPanelId _nextPanelId = 1;
    FDockFloatingWindowId _nextFloatingWindowId = 1;
    UIDockFloatingHost* _floatingHost = nullptr;
    UIDockSpace* _dockSpace = nullptr;
    std::function<void()> _onDockUpdated;
    std::vector<std::function<void()>> _onDockUpdatedListeners;
    std::function<void()> _onFloatingUpdated;
    std::vector<std::function<void()>> _onFloatingUpdatedListeners;

    [[nodiscard]] FFloatingWindow* findFloatingByIdMutable(FDockFloatingWindowId id);
};

} // namespace ya
