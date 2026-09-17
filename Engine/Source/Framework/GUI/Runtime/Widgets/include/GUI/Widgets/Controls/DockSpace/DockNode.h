#pragma once

#include "Core/Common/Types.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ya
{
using DockNodeId                                 = uint64_t;
using DockPanelId                                = uint64_t;
using FDockFloatingWindowId                      = uint64_t;
inline constexpr DockNodeId  kInvalidDockNodeId  = 0;
inline constexpr DockPanelId kInvalidDockPanelId = 0;
inline constexpr FDockFloatingWindowId kInvalidFloatingWindowId = 0;

enum class EDockNodeKind : uint8_t
{
    Split,
    /// Tab stack. JSON still accepts `"leaf"` for persist compatibility.
    Stack
};
enum class EDockSplitOrientation : uint8_t
{
    Vertical,
    Horizontal
};
enum class EDockCardinalSide : uint8_t
{
    West,
    East,
    North,
    South
};

/// Stack well role. Page stacks are the chrome page-tab host (hidden inner
/// tab strip). Tools stacks are ordinary window-tool docks. Generic is the
/// default for nested/owned docks that do not split those two wells.
enum class EDockLeafRole : uint8_t
{
    Generic = 0,
    Page,
    Tools,
};

struct FDockPanelRecord
{
    DockPanelId id = kInvalidDockPanelId;
    std::string stableKey;
    std::string title;
    bool        closable = true;
};

struct FDockNode
{
    EDockNodeKind              kind         = EDockNodeKind::Stack;
    DockNodeId                 id           = kInvalidDockNodeId;
    FDockNode*                 parent       = nullptr;
    EDockSplitOrientation      orientation  = EDockSplitOrientation::Vertical;
    float                      ratio        = 0.5f;
    float                      minExtent[2] = {10,10};
    std::unique_ptr<FDockNode> child[2];
    std::vector<DockPanelId>   panelIds;
    DockPanelId                selectedPanel       = kInvalidDockPanelId;
    /// Transient marker for `splitEmptyLeaf` while a Tools well is being
    /// adopted. Empty Generic/Tools leaves must still collapse — a visible
    /// empty stack ("drop tabs here") is forbidden. Page wells stay empty
    /// because they are chrome-only (no inner tab well).
    bool                       persistentEmptyLeaf = false;
    /// When true the leaf tab strip (title bar) is hidden; panel content still
    /// fills the leaf. A corner affordance or context menu restores the strip.
    bool                       bHideTabBar         = false;
    EDockLeafRole              leafRole            = EDockLeafRole::Generic;
};

/// Pure layout model: Split + Stack only. No WidgetTree, UIElement, or visual-control ownership.
struct YA_GUI_API FDockTreeModel
{
  private:
    std::unique_ptr<FDockNode>                        _root;
    std::unordered_map<DockPanelId, FDockPanelRecord> _panels;
    DockNodeId                                        _nextNodeId = 1;

  public:
    FDockTreeModel();
    [[nodiscard]] const FDockNode&        getRootNode() const { return *_root; }
    [[nodiscard]] FDockNode*              getRootNode() { return _root.get(); }
    [[nodiscard]] const FDockNode*        findNode(DockNodeId id) const;
    [[nodiscard]] FDockNode*              findNode(DockNodeId id);
    [[nodiscard]] const FDockNode*        findStackForPanel(DockPanelId id) const;
    [[nodiscard]] FDockNode*              findStackForPanel(DockPanelId id);
    [[nodiscard]] const FDockNode*        findLeafForPanel(DockPanelId id) const
    {
        return findStackForPanel(id);
    }
    [[nodiscard]] FDockNode* findLeafForPanel(DockPanelId id) { return findStackForPanel(id); }
    [[nodiscard]] const FDockPanelRecord* findPanel(DockPanelId id) const;

    bool registerPanel(FDockPanelRecord record);
    /// Dock `panelId` onto `leafId`. `kInvalidDockNodeId` means the root leaf,
    /// or the first leaf if the root has already been split.
    bool addPanel(DockPanelId panelId, DockNodeId leafId = kInvalidDockNodeId);
    bool selectPanel(DockPanelId panelId);
    bool setPanelClosable(DockPanelId panelId, bool closable);
    /// Move `panelId` onto `targetLeafId`. Same-leaf calls reorder tabs at
    /// `insertIndex` (SIZE_MAX appends). Cross-leaf calls insert then optionally
    /// collapse the emptied source.
    bool movePanel(DockPanelId panelId, DockNodeId targetLeafId, size_t insertIndex = SIZE_MAX, bool collapseSource = true);
    bool setSplitRatio(DockNodeId splitId, float ratio);
    bool setHideTabBar(DockNodeId leafId, bool hide);
    bool setLeafRole(DockNodeId leafId, EDockLeafRole role);
    [[nodiscard]] DockNodeId findFirstLeafWithRole(EDockLeafRole role) const;
    bool splitStack(DockNodeId targetStackId, EDockCardinalSide side, DockPanelId panelId, float newPanelRatio = 0.30f);
    bool splitLeaf(DockNodeId targetLeafId, EDockCardinalSide side, DockPanelId panelId, float newPanelRatio = 0.30f)
    {
        return splitStack(targetLeafId, side, panelId, newPanelRatio);
    }
    bool splitEmptyLeaf(DockNodeId targetLeafId, EDockCardinalSide side,
                        float newPanelRatio = 0.30f, bool persistentEmptyLeaf = true);
    /// Collapse empty Generic and Tools stacks. Page wells stay even when
    /// empty so chrome drop targets survive. Empty Tools wells are not a
    /// drop-target placeholder — they are recreated when a window tool is
    /// actually adopted. Persistent-empty leftovers are pruned with the leaf.
    void pruneEmptyGenericLeaves();
    /// Move a panel out of the dock tree (collapse the emptied source leaf)
    /// while keeping its registry record, so it can be re-docked later.
    /// Used by floating / tear-off workflows.
    bool                                  detachFromTree(DockPanelId panelId);
    bool                                  removePanel(DockPanelId panelId);
    [[nodiscard]] std::vector<DockNodeId> stackIds() const;
    [[nodiscard]] std::vector<DockNodeId> leafIds() const { return stackIds(); }
    [[nodiscard]] bool                    validateInvariants(std::string* error = nullptr) const;
    [[nodiscard]] size_t                  panelCount() const { return _panels.size(); }
    [[nodiscard]] const FDockPanelRecord* findPanelByStableKey(const std::string& stableKey) const;
    [[nodiscard]] nlohmann::json          exportLayoutJson() const;
    bool                                  importLayoutJson(const nlohmann::json& layout);

  private:
    std::unique_ptr<FDockNode> cloneNode(const FDockNode& source, FDockNode* parent) const;
    FDockNode*                 findNode(FDockNode* node, DockNodeId id) const;
    FDockNode*                 findStackForPanel(FDockNode* node, DockPanelId id) const;
    bool                       removePanelFromLeaf(DockPanelId panelId, FDockNode*& source);
    /// Collapse an emptied Generic/Tools leaf even if it was marked
    /// persistent. Page wells are left in place.
    void                       collapseEmptiedNonPageLeaf(FDockNode* leaf);
    void                       collapseEmptyLeaf(FDockNode* leaf);
    bool                       validateNode(const FDockNode& node, const FDockNode* expectedParent,
                                            std::unordered_map<DockPanelId, size_t>& seen,
                                            std::string*                             error) const;
    void                       collectLeafIds(const FDockNode& node, std::vector<DockNodeId>& result) const;
    void                       collectMountedPanelIds(const FDockNode& node, std::unordered_map<DockPanelId, size_t>& seen) const;
    bool                       importNodeFromJson(const nlohmann::json& nodeJson, FDockNode& node, std::string* error);
};
} // namespace ya
