#pragma once

#include "Core/Common/Types.h"

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ya
{
using DockNodeId = uint64_t;
using DockPanelId = uint64_t;
inline constexpr DockNodeId kInvalidDockNodeId = 0;
inline constexpr DockPanelId kInvalidDockPanelId = 0;
enum class EDockNodeKind : uint8_t { Split, Leaf };
enum class EDockSplitOrientation : uint8_t { Vertical, Horizontal };
enum class EDockCardinalSide : uint8_t { West, East, North, South };

struct FDockPanelRecord
{
    DockPanelId id = kInvalidDockPanelId;
    std::string stableKey;
    std::string title;
    bool closable = true;
};

struct FDockNode
{
    EDockNodeKind kind = EDockNodeKind::Leaf;
    DockNodeId id = kInvalidDockNodeId;
    FDockNode* parent = nullptr;
    EDockSplitOrientation orientation = EDockSplitOrientation::Vertical;
    float ratio = 0.5f;
    float minExtent[2] = {120.0f, 120.0f};
    std::unique_ptr<FDockNode> child[2];
    std::vector<DockPanelId> panelIds;
    DockPanelId selectedPanel = kInvalidDockPanelId;
    bool persistentEmptyLeaf = false;
};

/// Pure model: no WidgetTree, UIElement, or visual-control ownership.
struct YA_GUI_API FDockTreeModel
{
  public:
    FDockTreeModel();
    [[nodiscard]] const FDockNode& root() const { return *_root; }
    [[nodiscard]] FDockNode* root() { return _root.get(); }
    [[nodiscard]] const FDockNode* findNode(DockNodeId id) const;
    [[nodiscard]] FDockNode* findNode(DockNodeId id);
    [[nodiscard]] const FDockNode* findLeafForPanel(DockPanelId id) const;
    [[nodiscard]] FDockNode* findLeafForPanel(DockPanelId id);
    [[nodiscard]] const FDockPanelRecord* findPanel(DockPanelId id) const;
    bool registerPanel(FDockPanelRecord record);
    bool addPanel(DockPanelId panelId, DockNodeId leafId = kInvalidDockNodeId);
    bool movePanel(DockPanelId panelId, DockNodeId targetLeafId, size_t insertIndex = SIZE_MAX);
    bool splitLeaf(DockNodeId targetLeafId, EDockCardinalSide side, DockPanelId panelId,
                   float newPanelRatio = 0.30f);
    bool splitEmptyLeaf(DockNodeId targetLeafId, EDockCardinalSide side,
                        float newPanelRatio = 0.30f, bool persistentEmptyLeaf = true);
    bool removePanel(DockPanelId panelId);
    [[nodiscard]] std::vector<DockNodeId> leafIds() const;
    [[nodiscard]] bool validateInvariants(std::string* error = nullptr) const;
    [[nodiscard]] size_t panelCount() const { return _panels.size(); }

  private:
    std::unique_ptr<FDockNode> cloneNode(const FDockNode& source, FDockNode* parent) const;
    FDockNode* findNode(FDockNode* node, DockNodeId id) const;
    FDockNode* findLeafForPanel(FDockNode* node, DockPanelId id) const;
    bool removePanelFromLeaf(DockPanelId panelId, FDockNode*& source);
    void collapseEmptyLeaf(FDockNode* leaf);
    bool validateNode(const FDockNode& node, const FDockNode* expectedParent,
                      std::unordered_map<DockPanelId, size_t>& seen,
                      std::string* error) const;
    void collectLeafIds(const FDockNode& node, std::vector<DockNodeId>& result) const;
    std::unique_ptr<FDockNode> _root;
    std::unordered_map<DockPanelId, FDockPanelRecord> _panels;
    DockNodeId _nextNodeId = 1;
};
} // namespace ya
