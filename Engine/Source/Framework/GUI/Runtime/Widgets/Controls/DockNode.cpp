#include "GUI/Widgets/Controls/DockNode.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace ya
{
namespace
{
bool fail(std::string* error, std::string message)
{
    if (error) *error = std::move(message);
    return false;
}
}

FDockTreeModel::FDockTreeModel()
{
    _root = std::make_unique<FDockNode>();
    _root->id = _nextNodeId++;
}

std::unique_ptr<FDockNode> FDockTreeModel::cloneNode(const FDockNode& source, FDockNode* parent) const
{
    auto result = std::make_unique<FDockNode>();
    result->kind = source.kind;
    result->id = source.id;
    result->parent = parent;
    result->orientation = source.orientation;
    result->ratio = source.ratio;
    result->minExtent[0] = source.minExtent[0];
    result->minExtent[1] = source.minExtent[1];
    result->panelIds = source.panelIds;
    result->selectedPanel = source.selectedPanel;
    result->persistentEmptyLeaf = source.persistentEmptyLeaf;
    if (source.child[0]) result->child[0] = cloneNode(*source.child[0], result.get());
    if (source.child[1]) result->child[1] = cloneNode(*source.child[1], result.get());
    return result;
}

FDockNode* FDockTreeModel::findNode(FDockNode* node, DockNodeId id) const
{
    if (!node) return nullptr;
    if (node->id == id) return node;
    if (auto* result = findNode(node->child[0].get(), id)) return result;
    return findNode(node->child[1].get(), id);
}
const FDockNode* FDockTreeModel::findNode(DockNodeId id) const { return findNode(_root.get(), id); }
FDockNode* FDockTreeModel::findNode(DockNodeId id) { return findNode(_root.get(), id); }

FDockNode* FDockTreeModel::findLeafForPanel(FDockNode* node, DockPanelId id) const
{
    if (!node) return nullptr;
    if (node->kind == EDockNodeKind::Leaf) {
        return std::find(node->panelIds.begin(), node->panelIds.end(), id) != node->panelIds.end() ? node : nullptr;
    }
    if (auto* result = findLeafForPanel(node->child[0].get(), id)) return result;
    return findLeafForPanel(node->child[1].get(), id);
}
const FDockNode* FDockTreeModel::findLeafForPanel(DockPanelId id) const { return findLeafForPanel(_root.get(), id); }
FDockNode* FDockTreeModel::findLeafForPanel(DockPanelId id) { return findLeafForPanel(_root.get(), id); }

const FDockPanelRecord* FDockTreeModel::findPanel(DockPanelId id) const
{
    auto it = _panels.find(id);
    return it == _panels.end() ? nullptr : &it->second;
}

bool FDockTreeModel::registerPanel(FDockPanelRecord record)
{
    if (record.id == kInvalidDockPanelId || record.stableKey.empty() || _panels.contains(record.id)) return false;
    if (std::any_of(_panels.begin(), _panels.end(), [&](const auto& item) { return item.second.stableKey == record.stableKey; })) return false;
    _panels.emplace(record.id, std::move(record));
    return true;
}

bool FDockTreeModel::addPanel(DockPanelId panelId, DockNodeId leafId)
{
    if (!findPanel(panelId) || findLeafForPanel(panelId)) return false;
    FDockNode* leaf = leafId == kInvalidDockNodeId ? _root.get() : findNode(leafId);
    if (!leaf || leaf->kind != EDockNodeKind::Leaf) return false;
    leaf->panelIds.push_back(panelId);
    leaf->selectedPanel = panelId;
    return validateInvariants();
}

bool FDockTreeModel::selectPanel(DockPanelId panelId)
{
    FDockNode* leaf = findLeafForPanel(panelId);
    if (!leaf) {
        return false;
    }
    leaf->selectedPanel = panelId;
    return true;
}

bool FDockTreeModel::removePanelFromLeaf(DockPanelId panelId, FDockNode*& source)
{
    source = findLeafForPanel(panelId);
    if (!source) return false;
    auto it = std::find(source->panelIds.begin(), source->panelIds.end(), panelId);
    source->panelIds.erase(it);
    source->selectedPanel = source->panelIds.empty() ? kInvalidDockPanelId : source->panelIds.front();
    return true;
}

bool FDockTreeModel::movePanel(DockPanelId panelId, DockNodeId targetLeafId, size_t insertIndex, bool collapseSource)
{
    FDockNode* target = findNode(targetLeafId);
    FDockNode* source = findLeafForPanel(panelId);
    if (!findPanel(panelId) || !target || target->kind != EDockNodeKind::Leaf || !source || source == target) return false;

    auto backup = cloneNode(*_root, nullptr);
    const DockNodeId nextNodeId = _nextNodeId;
    if (!removePanelFromLeaf(panelId, source)) return false;
    if (insertIndex == SIZE_MAX || insertIndex > target->panelIds.size()) insertIndex = target->panelIds.size();
    target->panelIds.insert(target->panelIds.begin() + static_cast<std::ptrdiff_t>(insertIndex), panelId);
    target->selectedPanel = panelId;
    if (collapseSource && source->panelIds.empty() && !source->persistentEmptyLeaf) collapseEmptyLeaf(source);
    if (validateInvariants()) return true;
    _root = std::move(backup);
    _nextNodeId = nextNodeId;
    return false;
}

bool FDockTreeModel::setSplitRatio(DockNodeId splitId, float ratio)
{
    FDockNode* node = findNode(splitId);
    if (!node || node->kind != EDockNodeKind::Split || !std::isfinite(ratio)) {
        return false;
    }
    const float previous = node->ratio;
    node->ratio = std::clamp(ratio, 0.0f, 1.0f);
    if (!validateInvariants()) {
        node->ratio = previous;
        return false;
    }
    return true;
}

bool FDockTreeModel::splitLeaf(DockNodeId targetLeafId, EDockCardinalSide side, DockPanelId panelId, float newPanelRatio)
{
    auto backup = cloneNode(*_root, nullptr);
    const DockNodeId nextNodeId = _nextNodeId;
    FDockNode* target = findNode(targetLeafId);
    FDockNode* source = findLeafForPanel(panelId);
    if (!findPanel(panelId) || !target || target->kind != EDockNodeKind::Leaf) return false;
    if (source && source != target && !removePanelFromLeaf(panelId, source)) return false;
    auto oldPanels = target->panelIds;
    DockPanelId oldSelected = target->selectedPanel;
    const bool oldPersistent = target->persistentEmptyLeaf;
    if (source == target) {
        auto it = std::find(oldPanels.begin(), oldPanels.end(), panelId);
        if (it == oldPanels.end()) return false;
        oldPanels.erase(it);
        if (oldSelected == panelId) {
            oldSelected = oldPanels.empty() ? kInvalidDockPanelId : oldPanels.front();
        }
    }
    target->kind = EDockNodeKind::Split;
    target->orientation = (side == EDockCardinalSide::West || side == EDockCardinalSide::East)
                              ? EDockSplitOrientation::Vertical : EDockSplitOrientation::Horizontal;
    target->ratio = std::clamp(newPanelRatio, 0.0f, 1.0f);
    target->panelIds.clear();
    target->selectedPanel = kInvalidDockPanelId;
    target->persistentEmptyLeaf = false;
    target->child[0] = std::make_unique<FDockNode>();
    target->child[1] = std::make_unique<FDockNode>();
    target->child[0]->id = _nextNodeId++;
    target->child[1]->id = _nextNodeId++;
    target->child[0]->parent = target;
    target->child[1]->parent = target;
    FDockNode* newLeaf = (side == EDockCardinalSide::West || side == EDockCardinalSide::North) ? target->child[0].get() : target->child[1].get();
    FDockNode* oldLeaf = newLeaf == target->child[0].get() ? target->child[1].get() : target->child[0].get();
    newLeaf->panelIds = {panelId};
    newLeaf->selectedPanel = panelId;
    oldLeaf->panelIds = oldPanels;
    oldLeaf->selectedPanel = oldSelected;
    oldLeaf->persistentEmptyLeaf = oldPersistent;
    if (source != target && source && source->panelIds.empty() && !source->persistentEmptyLeaf) collapseEmptyLeaf(source);
    if (validateInvariants()) return true;
    _root = std::move(backup);
    _nextNodeId = nextNodeId;
    return false;
}

bool FDockTreeModel::splitLeafCorner(DockNodeId targetLeafId, EDockCorner corner, DockPanelId panelId, float newPanelRatio)
{
    auto backup = cloneNode(*_root, nullptr);
    const DockNodeId nextNodeId = _nextNodeId;
    FDockNode* target = findNode(targetLeafId);
    FDockNode* source = findLeafForPanel(panelId);
    if (!findPanel(panelId) || !target || target->kind != EDockNodeKind::Leaf) return false;
    if (source && source != target && !removePanelFromLeaf(panelId, source)) return false;

    auto oldPanels = target->panelIds;
    DockPanelId oldSelected = target->selectedPanel;
    const bool oldPersistent = target->persistentEmptyLeaf;
    if (source == target) {
        auto it = std::find(oldPanels.begin(), oldPanels.end(), panelId);
        if (it == oldPanels.end()) return false;
        oldPanels.erase(it);
        if (oldSelected == panelId) {
            oldSelected = oldPanels.empty() ? kInvalidDockPanelId : oldPanels.front();
        }
    }

    const bool bLeft = corner == EDockCorner::NorthWest || corner == EDockCorner::SouthWest;
    const bool bTop  = corner == EDockCorner::NorthWest || corner == EDockCorner::NorthEast;

    target->kind = EDockNodeKind::Split;
    target->orientation = EDockSplitOrientation::Vertical;
    target->ratio = std::clamp(newPanelRatio, 0.0f, 1.0f);
    target->panelIds.clear();
    target->selectedPanel = kInvalidDockPanelId;
    target->persistentEmptyLeaf = false;
    target->child[0] = std::make_unique<FDockNode>();
    target->child[1] = std::make_unique<FDockNode>();
    target->child[0]->id = _nextNodeId++;
    target->child[1]->id = _nextNodeId++;
    target->child[0]->parent = target;
    target->child[1]->parent = target;

    FDockNode* sideLeaf = bLeft ? target->child[0].get() : target->child[1].get();
    FDockNode* oldLeaf = bLeft ? target->child[1].get() : target->child[0].get();

    sideLeaf->kind = EDockNodeKind::Split;
    sideLeaf->orientation = EDockSplitOrientation::Horizontal;
    sideLeaf->ratio = std::clamp(newPanelRatio, 0.0f, 1.0f);
    sideLeaf->child[0] = std::make_unique<FDockNode>();
    sideLeaf->child[1] = std::make_unique<FDockNode>();
    sideLeaf->child[0]->id = _nextNodeId++;
    sideLeaf->child[1]->id = _nextNodeId++;
    sideLeaf->child[0]->parent = sideLeaf;
    sideLeaf->child[1]->parent = sideLeaf;

    FDockNode* newLeaf = bTop ? sideLeaf->child[0].get() : sideLeaf->child[1].get();
    FDockNode* emptyLeaf = bTop ? sideLeaf->child[1].get() : sideLeaf->child[0].get();

    newLeaf->panelIds = {panelId};
    newLeaf->selectedPanel = panelId;
    emptyLeaf->persistentEmptyLeaf = true;

    oldLeaf->panelIds = oldPanels;
    oldLeaf->selectedPanel = oldSelected;
    oldLeaf->persistentEmptyLeaf = oldPersistent;

    if (source && source != target && source->panelIds.empty() && !source->persistentEmptyLeaf) collapseEmptyLeaf(source);
    if (validateInvariants()) return true;
    _root = std::move(backup);
    _nextNodeId = nextNodeId;
    return false;
}

bool FDockTreeModel::splitEmptyLeaf(DockNodeId targetLeafId, EDockCardinalSide side,
                                    float newPanelRatio, bool persistentEmptyLeaf)
{
    auto backup = cloneNode(*_root, nullptr);
    const DockNodeId nextNodeId = _nextNodeId;
    FDockNode* target = findNode(targetLeafId);
    if (!target || target->kind != EDockNodeKind::Leaf) return false;

    const auto oldPanels = target->panelIds;
    const DockPanelId oldSelected = target->selectedPanel;
    const bool oldPersistent = target->persistentEmptyLeaf;
    target->kind = EDockNodeKind::Split;
    target->orientation = (side == EDockCardinalSide::West || side == EDockCardinalSide::East)
                              ? EDockSplitOrientation::Vertical : EDockSplitOrientation::Horizontal;
    target->ratio = std::clamp(newPanelRatio, 0.0f, 1.0f);
    target->panelIds.clear();
    target->selectedPanel = kInvalidDockPanelId;
    target->persistentEmptyLeaf = false;
    target->child[0] = std::make_unique<FDockNode>();
    target->child[1] = std::make_unique<FDockNode>();
    target->child[0]->id = _nextNodeId++;
    target->child[1]->id = _nextNodeId++;
    target->child[0]->parent = target;
    target->child[1]->parent = target;
    FDockNode* newLeaf = (side == EDockCardinalSide::West || side == EDockCardinalSide::North)
                             ? target->child[0].get() : target->child[1].get();
    FDockNode* oldLeaf = newLeaf == target->child[0].get() ? target->child[1].get() : target->child[0].get();
    newLeaf->persistentEmptyLeaf = persistentEmptyLeaf;
    oldLeaf->panelIds = oldPanels;
    oldLeaf->selectedPanel = oldSelected;
    oldLeaf->persistentEmptyLeaf = oldPersistent;
    if (validateInvariants()) return true;
    _root = std::move(backup);
    _nextNodeId = nextNodeId;
    return false;
}

void FDockTreeModel::collapseEmptyLeaf(FDockNode* leaf)
{
    if (!leaf || leaf == _root.get() || leaf->kind != EDockNodeKind::Leaf || !leaf->panelIds.empty() || leaf->persistentEmptyLeaf || !leaf->parent) return;
    FDockNode* parent = leaf->parent;
    std::unique_ptr<FDockNode> sibling = parent->child[0].get() == leaf ? std::move(parent->child[1]) : std::move(parent->child[0]);
    sibling->parent = parent->parent;
    if (!parent->parent) {
        _root = std::move(sibling);
        _root->parent = nullptr;
        return;
    }
    FDockNode* grand = parent->parent;
    std::unique_ptr<FDockNode>& parentSlot = grand->child[0].get() == parent ? grand->child[0] : grand->child[1];
    parentSlot = std::move(sibling);
    collapseEmptyLeaf(parentSlot.get());
}

void FDockTreeModel::collectLeafIds(const FDockNode& node, std::vector<DockNodeId>& result) const
{
    if (node.kind == EDockNodeKind::Leaf) {
        result.push_back(node.id);
        return;
    }
    if (node.child[0]) collectLeafIds(*node.child[0], result);
    if (node.child[1]) collectLeafIds(*node.child[1], result);
}

std::vector<DockNodeId> FDockTreeModel::leafIds() const
{
    std::vector<DockNodeId> result;
    if (_root) collectLeafIds(*_root, result);
    return result;
}

bool FDockTreeModel::removePanel(DockPanelId panelId)
{
    if (!findPanel(panelId) || !findLeafForPanel(panelId)) return false;
    auto backup = cloneNode(*_root, nullptr);
    const auto panelsBackup = _panels;
    const DockNodeId nextNodeId = _nextNodeId;
    FDockNode* source = nullptr;
    if (!removePanelFromLeaf(panelId, source)) return false;
    _panels.erase(panelId);
    if (source->panelIds.empty() && !source->persistentEmptyLeaf) collapseEmptyLeaf(source);
    if (validateInvariants()) return true;
    _root = std::move(backup);
    _panels = panelsBackup;
    _nextNodeId = nextNodeId;
    return false;
}

bool FDockTreeModel::validateNode(const FDockNode& node, const FDockNode* expectedParent,
                                  std::unordered_map<DockPanelId, size_t>& seen, std::string* error) const
{
    if (node.parent != expectedParent) return fail(error, std::format("node {} has invalid parent", node.id));
    if (node.kind == EDockNodeKind::Leaf) {
        if (node.child[0] || node.child[1]) return fail(error, std::format("leaf {} has children", node.id));
        for (DockPanelId panelId : node.panelIds) {
            if (!findPanel(panelId)) return fail(error, std::format("leaf {} references unknown panel {}", node.id, panelId));
            ++seen[panelId];
        }
        if (node.selectedPanel != kInvalidDockPanelId && std::find(node.panelIds.begin(), node.panelIds.end(), node.selectedPanel) == node.panelIds.end()) return fail(error, std::format("leaf {} selected panel is not present", node.id));
        return true;
    }
    if (!node.child[0] || !node.child[1] || !std::isfinite(node.ratio) || node.ratio < 0.0f || node.ratio > 1.0f || node.minExtent[0] < 0.0f || node.minExtent[1] < 0.0f || !node.panelIds.empty()) return fail(error, std::format("split {} has invalid shape or geometry", node.id));
    return validateNode(*node.child[0], &node, seen, error) && validateNode(*node.child[1], &node, seen, error);
}

bool FDockTreeModel::validateInvariants(std::string* error) const
{
    std::unordered_map<DockPanelId, size_t> seen;
    if (!_root || _root->parent != nullptr || !validateNode(*_root, nullptr, seen, error)) return false;
    for (const auto& [panelId, record] : _panels) {
        if (seen[panelId] > 1) return fail(error, std::format("panel {} occurs {} times", panelId, seen[panelId]));
        if (record.id != panelId || record.stableKey.empty()) return fail(error, std::format("panel {} record is invalid", panelId));
    }
    return true;
}
} // namespace ya
