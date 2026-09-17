#pragma once

#include "GUI/Widgets/Controls/DockSpace/DockNode.h"
#include "GUI/Widgets/Controls/DockSpace/DockDropTarget.h"

#include <cstdint>
#include <glm/glm.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ya
{

struct UIElement;
struct UIDockFloatingHost;
struct UIDockSpace;

/// How a floating placement is projected. Overlay stays in the source
/// native window / WidgetTree. NativeWindow is a coordinator record only
/// (MW-702/703); `UIDockFloatingHost` must not treat it as an OS window.
enum class EDockFloatingProjection : uint8_t
{
    InProcessOverlay,
    NativeWindow,
};

/// Which dock model the panel was torn from. Opaque to editor types:
/// window-root vs owned-nested docks map onto this enum.
enum class EDockSourceScope : uint8_t
{
    WindowRoot,
    EditorOwned,
};

/// Coordinate space of a placement's `pos`. Overlay geometry is always
/// tree-local (Popup). NativeWindow screen bounds must be tagged `Screen`;
/// missing tags never promote overlay/Popup coordinates to OS window origin.
enum class EDockGeometrySpace : uint8_t
{
    TreeLocal,
    Screen,
};

/// Shared dock session: TabRegistry + DockLayout + overlay/native placement.
/// Not a widget and not a native window.
///
/// Owns the tab registry, `FDockTreeModel` layout, floating *placement* records,
/// policy flags, and layout JSON. Projections that read this session:
///   - `UIDockSpace` — DockArea view of splits + stack projections
///   - `UIDockFloatingHost` — InProcessOverlay only (Popup layer, same tree)
/// NativeWindow placements are consumed by a GUI window coordinator (MW-702),
/// not by `UIDockFloatingHost`. This object never creates an OS window.
struct YA_GUI_API FDockContext
{
    struct FPanel
    {
        DockPanelId                id = kInvalidDockPanelId;
        std::string                name;
        std::shared_ptr<UIElement> widget;
        uint32_t                   ownerEditorId = 0;
        std::string                documentKey;
    };

    /// Tab identity + live widget. Which stack holds which TabId is `layout()`.
    struct FTabRegistry
    {
        std::unordered_map<DockPanelId, FPanel> panels;
        DockPanelId                             nextPanelId = 1;

        [[nodiscard]] FPanel* find(DockPanelId id)
        {
            auto it = panels.find(id);
            return it == panels.end() ? nullptr : &it->second;
        }
        [[nodiscard]] const FPanel* find(DockPanelId id) const
        {
            auto it = panels.find(id);
            return it == panels.end() ? nullptr : &it->second;
        }
    };

    /// Placement record for a torn-off panel set. Overlay `pos` is tree-local.
    /// NativeWindow `pos` is screen origin only when `geometrySpace` is Screen.
    struct FDockFloatingPlacement
    {
        FDockFloatingWindowId    id            = kInvalidFloatingWindowId;
        std::vector<DockPanelId> panelIds;
        DockPanelId              activePanelId = kInvalidDockPanelId;
        glm::vec2                pos{0.0f, 0.0f};
        glm::vec2                size{320.0f, 240.0f};
        bool                     bHideTabBar = false;
        EDockFloatingProjection  projection    = EDockFloatingProjection::InProcessOverlay;
        EDockGeometrySpace       geometrySpace = EDockGeometrySpace::TreeLocal;
        EDockSourceScope         sourceScope   = EDockSourceScope::WindowRoot;
        uint32_t                 targetWindowId = 0;
        uint32_t                 ownerEditorId  = 0;
        std::string              documentKey;
    };
    using FFloatingWindow = FDockFloatingPlacement;

    /// Panel payload after leaving this dock (widget is detached from any tree).
    struct FDockExtractedPanel
    {
        std::string                stableKey;
        std::string                title;
        bool                       closable      = true;
        uint32_t                   ownerEditorId = 0;
        std::string                documentKey;
        std::shared_ptr<UIElement> widget;
    };

    /// Dock policy switches (central on/off for the whole session).
    bool bAllowDocking  = true;   // panels may dock into DockSpaces
    bool bAllowFloating = false;  // floating placements may exist
    bool bAllowTearOff  = false;  // drag-out may create an overlay placement
    EDockSourceScope sourceScope = EDockSourceScope::WindowRoot;
    uint32_t         hostWindowId = 0;
    /// Optional import filter. Empty means every panel may be adopted.
    /// Receives opaque `stableKey` / `ownerEditorId` / `documentKey` only;
    /// this object still does not know editor types.
    std::function<bool(std::string_view, uint32_t, std::string_view)> canAdoptPanel;
    /// Optional per-stack drop filter. `bMerge` is true for TabWell / center
    /// merge and false for cardinal split. Empty means every stack may receive
    /// the panel. Receives opaque keys only; no editor types.
    std::function<bool(std::string_view, uint32_t, std::string_view, DockNodeId, bool)> canAdoptOntoLeaf;
    /// Optional leaf picker for add/transfer. Empty uses the focused leaf.
    std::function<DockNodeId(std::string_view, uint32_t, std::string_view)> chooseAdoptLeaf;
    /// Optional NoTarget handler. DockSpace calls this instead of overlay
    /// `tearOffPanel` when set. Return true if handled (native window created
    /// or rejected). Return false to fall back to InProcessOverlay. This
    /// object still never creates an OS window.
    std::function<bool(DockPanelId, const glm::vec2& pos, const glm::vec2& size)> realizeNoTargetTearOff;
    /// Empty-strip double-click on a dock leaf tab bar. Product chrome may
    /// bind maximize here; this object never creates an OS window.
    std::function<void()> onTabBarDoubleClick;

    [[nodiscard]] bool acceptsImportedPanel(std::string_view stableKey,
                                            uint32_t ownerEditorId,
                                            std::string_view documentKey) const
    {
        return !canAdoptPanel || canAdoptPanel(stableKey, ownerEditorId, documentKey);
    }

    [[nodiscard]] bool acceptsLeafDrop(std::string_view stableKey,
                                       uint32_t ownerEditorId,
                                       std::string_view documentKey,
                                       DockNodeId leafId,
                                       bool bMerge) const
    {
        return !canAdoptOntoLeaf ||
               canAdoptOntoLeaf(stableKey, ownerEditorId, documentKey, leafId, bMerge);
    }

    [[nodiscard]] DockNodeId adoptLeafFor(std::string_view stableKey,
                                          uint32_t ownerEditorId,
                                          std::string_view documentKey) const
    {
        if (!chooseAdoptLeaf) {
            return kInvalidDockNodeId;
        }
        return chooseAdoptLeaf(stableKey, ownerEditorId, documentKey);
    }

    /// Register a panel and dock it into the dock tree's root leaf. Returns the
    /// stable panel id (kInvalidDockPanelId on failure).
    DockPanelId addPanel(const std::string& name, std::shared_ptr<UIElement> widget);
    DockPanelId addPanel(const std::string& stableKey, const std::string& title, std::shared_ptr<UIElement> widget);

    // === Floating (Phase 5) ===
    /// Bind the overlay host that presents InProcessOverlay placements.
    void setFloatingHost(UIDockFloatingHost* host) { _floatingHost = host; }
    [[nodiscard]] UIDockFloatingHost* floatingHost() const { return _floatingHost; }

    /// Bind the DockSpace that projects this context's dock tree. Used by
    /// floating-window tab drags to drive the dock drop-preview (chooser),
    /// since the DockSpace lives on a different layer than the floating host.
    void setDockSpace(UIDockSpace* space) { _dockSpace = space; }
    [[nodiscard]] UIDockSpace* dockSpace() const { return _dockSpace; }

    bool setPanelIdentity(DockPanelId id, uint32_t ownerEditorId, std::string documentKey);
    bool setPanelIdentity(std::string_view stableKey, uint32_t ownerEditorId, std::string documentKey);

    /// Tear a panel out of the dock tree into a floating placement.
    /// Default projection is InProcessOverlay (`targetWindowId` = this host).
    /// NativeWindow records the placement with `targetWindowId` 0 until a
    /// GUI window coordinator binds a session; this object never creates an
    /// OS window.
    FDockFloatingWindowId tearOffPanel(DockPanelId panelId,
                                       const glm::vec2& pos,
                                       const glm::vec2& size,
                                       EDockFloatingProjection projection = EDockFloatingProjection::InProcessOverlay);
    /// Bind a NativeWindow placement to an existing coordinator session id.
    /// Overlay placements return false.
    bool bindFloatingTargetWindow(FDockFloatingWindowId id, uint32_t windowId);
    /// Detach a panel from this dock (visual tree + model) without destroying
    /// the widget. Opaque identity fields are preserved. Does not interpret
    /// editor types.
    [[nodiscard]] std::optional<FDockExtractedPanel> extractPanel(DockPanelId id);
    /// Register an extracted panel. Does not mount it into the dock tree;
    /// the caller places it (drop) or `transferPanelTo` adds it to a leaf.
    DockPanelId adoptPanel(FDockExtractedPanel extracted);
    /// Move a panel to another dock context: extract → adopt → root/focused
    /// leaf. Never dual-mounts the live widget. Duplicate `stableKey` fails.
    DockPanelId transferPanelTo(FDockContext& target, DockPanelId panelId);
    /// Transfer every panel in a NativeWindow placement into `target`.
    bool transferNativePlacementTo(FDockContext& target, FDockFloatingWindowId placementId);
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
    [[nodiscard]] const std::vector<FDockFloatingPlacement>& floatingPlacements() const { return _floating; }
    [[nodiscard]] const std::vector<FFloatingWindow>& floatingWindows() const { return _floating; }
    bool setFloatingProjection(FDockFloatingWindowId id, EDockFloatingProjection projection);
    bool setFloatingGeometrySpace(FDockFloatingWindowId id, EDockGeometrySpace space);
    /// Update a floating window's on-screen position (called as the window moves).
    void setFloatingWindowPos(FDockFloatingWindowId id, const glm::vec2& pos);
    /// Update a floating window's logical position and size without notifying listeners.
    void setFloatingWindowRect(FDockFloatingWindowId id, const glm::vec2& pos, const glm::vec2& size);
    void setFloatingWindowActivePanel(FDockFloatingWindowId id, DockPanelId panelId);
    void setFloatingHideTabBar(FDockFloatingWindowId id, bool hide);

    /// Serialize dock topology (`root` + overlay `floating`) separately from
    /// native window topology (`windows`). Overlay geometry is tree-local.
    [[nodiscard]] nlohmann::json exportLayoutJson() const;
    /// Restore docked tree, overlay `floating`, then native `windows`.
    /// Missing arrays are empty. Legacy `floating` without `projection` is
    /// InProcessOverlay + tree-local (never screen). Unknown panel keys fail.
    bool importLayoutJson(const nlohmann::json& layout);

    /// The DockSpace re-projects via `syncProjection(Structure)` when the dock
    /// *structure* changes (drop / tear-off / import / close that prunes a
    /// stack). `addPanel` / tab selection use `syncProjection(Stack)` so they
    /// do not rematerialize. Split-ratio and hide-tab-bar update live chrome
    /// and call notifyDockLayoutListeners() so persist runs without rebuilding.
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
    /// Record the stack that should receive newly spawned panels (tab click /
    /// activate). No-op if `stackId` is not a live stack.
    void rememberFocusedStack(DockNodeId stackId);
    void rememberFocusedLeaf(DockNodeId leafId) { rememberFocusedStack(leafId); }
    [[nodiscard]] DockNodeId lastFocusedStackId() const { return _lastFocusedStackId; }
    [[nodiscard]] DockNodeId lastFocusedLeafId() const { return _lastFocusedStackId; }
    /// Collect unique panel keys from a layout JSON (docked tree + overlay
    /// floating + native `windows`).
    [[nodiscard]] static std::vector<std::string> collectLayoutPanelKeys(const nlohmann::json& layout);
    /// Drop panel keys not in `knownKeys` from leaves, overlay `floating`, and
    /// native `windows` so a strict import can succeed after spawners skip
    /// unknown tabs.
    [[nodiscard]] static nlohmann::json sanitizeLayoutJson(nlohmann::json layout,
                                                           const std::unordered_set<std::string>& knownKeys);
    [[nodiscard]] std::vector<std::string> panelStableKeys() const;
    bool setPanelClosable(DockPanelId id, bool closable);
    bool setPanelClosable(std::string_view stableKey, bool closable);
    /// Close a closable panel: unlink it from the dock tree or floating window
    /// and drop the registry record. Non-closable panels return false.
    bool closePanel(DockPanelId id);
    bool closePanel(std::string_view stableKey);
    [[nodiscard]] FTabRegistry& tabs() { return _tabs; }
    [[nodiscard]] const FTabRegistry& tabs() const { return _tabs; }
    [[nodiscard]] FDockTreeModel& layout() { return _layout; }
    [[nodiscard]] const FDockTreeModel& layout() const { return _layout; }
    [[nodiscard]] FDockTreeModel& dockModel() { return _layout; }
    [[nodiscard]] const FDockTreeModel& dockModel() const { return _layout; }
    /// Apply a drop target produced by a stack/well projection. Does not
    /// rematerialize projections; the caller rebuilds if `Applied`.
    EDockDropCommit commitDrop(DockPanelId panelId, const FDockDropTarget& target);

private:
    FTabRegistry _tabs;
    FDockTreeModel _layout;
    std::vector<FFloatingWindow> _floating;
    DockNodeId _lastFocusedStackId = kInvalidDockNodeId;
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
