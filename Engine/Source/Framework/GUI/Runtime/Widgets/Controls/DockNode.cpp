#include "GUI/Widgets/Controls/DockNode.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <nlohmann/json.hpp>

namespace ya
{
namespace
{
bool fail(std::string* error, std::string message)
{
    if (error) *error = std::move(message);
    return false;
}

nlohmann::json exportNode(const FDockTreeModel& model, const FDockNode& node)
{
    if (node.kind == EDockNodeKind::Leaf) {
        nlohmann::json result = nlohmann::json::object();
        result["kind"] = "leaf";
        result["panels"] = nlohmann::json::array();
        for (const DockPanelId panelId : node.panelIds) {
            if (const FDockPanelRecord* record = model.findPanel(panelId)) {
                result["panels"].push_back(record->stableKey);
            }
        }
        if (const FDockPanelRecord* selected = model.findPanel(node.selectedPanel)) {
            result["selected"] = selected->stableKey;
        }
        if (node.persistentEmptyLeaf) {
            result["persistentEmpty"] = true;
        }
        return result;
    }

    nlohmann::json result = nlohmann::json::object();
    result["kind"] = "split";
    result["orientation"] = node.orientation == EDockSplitOrientation::Vertical ? "vertical" : "horizontal";
    result["ratio"] = node.ratio;
    result["minExtent"] = nlohmann::json::array({node.minExtent[0], node.minExtent[1]});
    result["children"] = nlohmann::json::array({
        exportNode(model, *node.child[0]),
        exportNode(model, *node.child[1]),
    });
    return result;
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
        // Splitting a panel out of its own leaf must leave the "old" side
        // non-empty: otherwise we fabricate a non-persistent empty leaf.
        if (oldPanels.empty()) {
            _root = std::move(backup);
            _nextNodeId = nextNodeId;
            return false;
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

bool FDockTreeModel::detachFromTree(DockPanelId panelId)
{
    if (!findPanel(panelId) || !findLeafForPanel(panelId)) return false;
    auto backup = cloneNode(*_root, nullptr);
    const DockNodeId nextNodeId = _nextNodeId;
    FDockNode* source = nullptr;
    if (!removePanelFromLeaf(panelId, source)) return false;
    if (source->panelIds.empty() && !source->persistentEmptyLeaf) collapseEmptyLeaf(source);
    if (validateInvariants()) return true;
    _root = std::move(backup);
    _nextNodeId = nextNodeId;
    return false;
}

const FDockPanelRecord* FDockTreeModel::findPanelByStableKey(const std::string& stableKey) const
{
    for (const auto& [id, record] : _panels) {
        if (record.stableKey == stableKey) {
            return &record;
        }
    }
    return nullptr;
}

nlohmann::json FDockTreeModel::exportLayoutJson() const
{
    nlohmann::json layout = nlohmann::json::object();
    layout["version"] = 1;
    layout["root"] = exportNode(*this, *_root);
    return layout;
}

bool FDockTreeModel::importNodeFromJson(const nlohmann::json& nodeJson, FDockNode& node, std::string* error)
{
    const std::string kind = nodeJson.value("kind", "");
    if (kind == "leaf") {
        node.kind = EDockNodeKind::Leaf;
        node.panelIds.clear();
        node.selectedPanel = kInvalidDockPanelId;
        node.persistentEmptyLeaf = nodeJson.value("persistentEmpty", false);
        if (!nodeJson.contains("panels") || !nodeJson["panels"].is_array()) {
            return fail(error, "dock layout leaf is missing panels array");
        }
        for (const nlohmann::json& panelKeyJson : nodeJson["panels"]) {
            if (!panelKeyJson.is_string()) {
                return fail(error, "dock layout panel key must be a string");
            }
            const FDockPanelRecord* record = findPanelByStableKey(panelKeyJson.get<std::string>());
            if (!record) {
                return fail(error, std::format("dock layout references unknown panel '{}'", panelKeyJson.get<std::string>()));
            }
            node.panelIds.push_back(record->id);
        }
        if (nodeJson.contains("selected")) {
            if (!nodeJson["selected"].is_string()) {
                return fail(error, "dock layout selected panel must be a string");
            }
            const FDockPanelRecord* selected = findPanelByStableKey(nodeJson["selected"].get<std::string>());
            if (!selected) {
                return fail(error, std::format("dock layout references unknown selected panel '{}'", nodeJson["selected"].get<std::string>()));
            }
            node.selectedPanel = selected->id;
        }
        else if (!node.panelIds.empty()) {
            node.selectedPanel = node.panelIds.front();
        }
        return true;
    }

    if (kind != "split") {
        return fail(error, "dock layout node kind must be leaf or split");
    }
    if (!nodeJson.contains("children") || !nodeJson["children"].is_array() || nodeJson["children"].size() != 2) {
        return fail(error, "dock layout split must have exactly two children");
    }

    node.kind = EDockNodeKind::Split;
    const std::string orientation = nodeJson.value("orientation", "vertical");
    node.orientation = orientation == "horizontal" ? EDockSplitOrientation::Horizontal : EDockSplitOrientation::Vertical;
    node.ratio = nodeJson.value("ratio", 0.5f);
    if (nodeJson.contains("minExtent") && nodeJson["minExtent"].is_array() && nodeJson["minExtent"].size() == 2) {
        node.minExtent[0] = nodeJson["minExtent"][0].get<float>();
        node.minExtent[1] = nodeJson["minExtent"][1].get<float>();
    }
    else {
        node.minExtent[0] = 120.0f;
        node.minExtent[1] = 120.0f;
    }

    node.child[0] = std::make_unique<FDockNode>();
    node.child[1] = std::make_unique<FDockNode>();
    node.child[0]->id = _nextNodeId++;
    node.child[1]->id = _nextNodeId++;
    node.child[0]->parent = &node;
    node.child[1]->parent = &node;
    if (!importNodeFromJson(nodeJson["children"][0], *node.child[0], error)) {
        return false;
    }
    if (!importNodeFromJson(nodeJson["children"][1], *node.child[1], error)) {
        return false;
    }
    return true;
}

void FDockTreeModel::collectMountedPanelIds(const FDockNode& node, std::unordered_map<DockPanelId, size_t>& seen) const
{
    if (node.kind == EDockNodeKind::Leaf) {
        for (const DockPanelId panelId : node.panelIds) {
            ++seen[panelId];
        }
        return;
    }
    if (node.child[0]) {
        collectMountedPanelIds(*node.child[0], seen);
    }
    if (node.child[1]) {
        collectMountedPanelIds(*node.child[1], seen);
    }
}

bool FDockTreeModel::importLayoutJson(const nlohmann::json& layout)
{
    if (layout.value("version", 0) != 1 || !layout.contains("root")) {
        return false;
    }

    auto backupRoot = cloneNode(*_root, nullptr);
    const DockNodeId nextNodeIdBackup = _nextNodeId;

    _nextNodeId = 1;
    _root = std::make_unique<FDockNode>();
    _root->id = _nextNodeId++;
    _root->parent = nullptr;

    std::string error;
    if (!importNodeFromJson(layout["root"], *_root, &error)) {
        _root = std::move(backupRoot);
        _nextNodeId = nextNodeIdBackup;
        return false;
    }

    std::unordered_map<DockPanelId, size_t> mounted;
    collectMountedPanelIds(*_root, mounted);
    const auto leaves = leafIds();
    const DockNodeId orphanLeafId = leaves.empty() ? kInvalidDockNodeId : leaves.front();
    for (const auto& [panelId, record] : _panels) {
        (void)record;
        if (mounted.contains(panelId)) {
            continue;
        }
        if (orphanLeafId == kInvalidDockNodeId || !addPanel(panelId, orphanLeafId)) {
            _root = std::move(backupRoot);
            _nextNodeId = nextNodeIdBackup;
            return false;
        }
    }

    if (!validateInvariants(&error)) {
        _root = std::move(backupRoot);
        _nextNodeId = nextNodeIdBackup;
        return false;
    }
    return true;
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
