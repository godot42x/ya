#include "GameEditor/Inspector/PropertyGraph.h"
#include "GameEditor/Inspector/PropertyProjection.h"
#include "GameEditor/Inspector/PropertyGraphBuilder.h"
#include "GameEditor/Inspector/PropertyEditorMetadata.h"

#include "Core/Reflection/MetadataSupport.h"
#include "Core/Reflection/PropertyAccessor.h"
#include "ECS/Component.h"
#include "ECS/ECSRegistry.h"
#include "reflects-core/lib.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace ya
{
namespace
{

std::string makeDisplayName(std::string_view name)
{
    while (!name.empty() && name.front() == '_') {
        name.remove_prefix(1);
    }
    if (name.size() > 1 && (name.front() == 'b' || name.front() == 'm') &&
        std::isupper(static_cast<unsigned char>(name[1]))) {
        name.remove_prefix(1);
    }
    std::string result;
    result.reserve(name.size() + 4);
    for (size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if (i > 0 && std::isupper(static_cast<unsigned char>(c)) &&
            std::islower(static_cast<unsigned char>(name[i - 1]))) {
            result.push_back(' ');
        }
        result.push_back(i == 0 ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c);
    }
    return result;
}

std::string prettySegment(std::string_view part)
{
    if (part.empty()) {
        return {};
    }
    const size_t bracket = part.find('[');
    if (bracket != std::string_view::npos) {
        return makeDisplayName(part.substr(0, bracket)) + " " + std::string(part.substr(bracket));
    }
    return makeDisplayName(part);
}

std::string joinGroups(const std::vector<std::string>& parts)
{
    std::string display;
    for (const std::string& part : parts) {
        if (part.empty()) {
            continue;
        }
        if (!display.empty()) {
            display += " / ";
        }
        display += part;
    }
    return display;
}

bool isLeafEditableType(const PropertyNode& node)
{
    if (node.kind == PropertyNode::Kind::Sequence || node.kind == PropertyNode::Kind::Map) {
        return true;
    }
    return reflection::PropertyAccessor::isLeafValueType(node.valueType);
}

void installDefaultComponentEditHooks(PropertyGraph& graph)
{
    // Widgets and other non-ECS graphs must not be treated as IComponent.
    if (ECSRegistry::get().getComponentOps(graph.getOwnerType()) == nullptr) {
        return;
    }
    const std::vector<void*> instances = graph.getRootInstances();
    for (PropertyNode& node : graph.getNodesMutable()) {
        node.binding.setChangeHook([instances]() {
            for (void* instance : instances) {
                if (instance) {
                    static_cast<IComponent*>(instance)->onEdit();
                }
            }
        });
    }
}

} // namespace

FPropertyLabel propertyLabelFromPath(std::string_view path)
{
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= path.size()) {
        const size_t end = path.find('.', start);
        const std::string_view part =
            path.substr(start, end == std::string_view::npos ? path.size() - start : end - start);
        if (!part.empty()) {
            parts.push_back(prettySegment(part));
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }

    FPropertyLabel label;
    if (parts.empty()) {
        return label;
    }
    label.displayName = parts.back();
    parts.pop_back();
    label.group = joinGroups(parts);
    return label;
}

PropertyGraph PropertyGraph::build(type_index_t ownerType, std::vector<void*> instances)
{
    PropertyGraph graph;
    graph._ownerType = ownerType;
    graph._rootInstances = instances;

    std::vector<PropertyGraphBuilder::FLeaf> leaves;
    PropertyGraphBuilder::collectLeaves(ownerType, instances, leaves);
    graph._nodes.reserve(leaves.size());
    for (PropertyGraphBuilder::FLeaf& leaf : leaves) {
        if (!leaf.slot.property) {
            continue;
        }
        PropertyNode node;
        node.name = leaf.path;
        const FPropertyLabel label = propertyLabelFromPath(leaf.path);
        node.displayName = label.displayName;
        node.group = label.group;
        if (leaf.slot.property && leaf.slot.property->metadata.hasMeta("category")) {
            try {
                node.category = leaf.slot.property->metadata.get<std::string>("category");
            }
            catch (...) {
            }
        }
        node.valueType = reflection::PropertyAccessor::valueType(leaf.slot);
        node.kind = leaf.role == PropertyGraphBuilder::ELeafRole::Sequence
            ? PropertyNode::Kind::Sequence
            : leaf.role == PropertyGraphBuilder::ELeafRole::Map
                ? PropertyNode::Kind::Map
                : PropertyNode::Kind::Value;
        node.binding = PropertyHandle(leaf.ownerType, std::move(leaf.ownerInstances), leaf.slot);
        node.binding.setOwnerPath(std::move(leaf.ownerPath));
        node.bEditable = node.binding.isEditable();
        node.bVisible = true;
        node.bInstanceEditable = leaf.slot.property && leaf.slot.property->metadata.hasFlag(FieldFlags::InstanceEditable);
        node.bColor = leaf.slot.property && PropertyEditorMetadata::isColor(*leaf.slot.property);
        graph._nodes.push_back(std::move(node));
    }
    return graph;
}

PropertyGraph PropertyGraph::project(type_index_t ownerType, std::vector<void*> instances)
{
    registerBuiltinPropertyProjections();
    PropertyGraph graph = build(ownerType, std::move(instances));
    // Default: any reflected write on an ECS component calls IComponent::onEdit().
    // Type-specific projections (materials, Transform setters) may replace the hook.
    installDefaultComponentEditHooks(graph);
    PropertyProjectionRegistry::instance().apply(ownerType, graph);
    return graph;
}

bool PropertyGraph::hasRetainedEditors() const
{
    for (const PropertyNode& node : _nodes) {
        if (node.bVisible && isLeafEditableType(node)) {
            return true;
        }
    }
    return false;
}

PropertyNode* PropertyGraph::find(std::string_view name)
{
    auto it = std::find_if(_nodes.begin(), _nodes.end(), [name](const PropertyNode& node) { return node.name == name; });
    return it == _nodes.end() ? nullptr : &*it;
}

const PropertyNode* PropertyGraph::find(std::string_view name) const
{
    auto it = std::find_if(_nodes.begin(), _nodes.end(), [name](const PropertyNode& node) { return node.name == name; });
    return it == _nodes.end() ? nullptr : &*it;
}

} // namespace ya
