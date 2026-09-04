#include "GameEditor/Inspector/PropertyGraph.h"
#include "GameEditor/Inspector/PropertyProjection.h"

#include "Core/Reflection/MetadataSupport.h"
#include "Core/Reflection/PropertyAccessor.h"
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
    std::string result;
    result.reserve(name.size());
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

std::string displayNameFromPath(std::string_view path)
{
    std::string display;
    size_t start = 0;
    while (start <= path.size()) {
        const size_t end = path.find('.', start);
        const std::string_view part = path.substr(start, end == std::string_view::npos ? path.size() - start : end - start);
        if (!part.empty()) {
            if (!display.empty()) {
                display += " / ";
            }
            const size_t bracket = part.find('[');
            if (bracket != std::string_view::npos) {
                display += makeDisplayName(part.substr(0, bracket));
                display += " ";
                display += part.substr(bracket);
            }
            else {
                display += makeDisplayName(part);
            }
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
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

} // namespace

PropertyGraph PropertyGraph::build(type_index_t ownerType, std::vector<void*> instances)
{
    PropertyGraph graph;
    graph._ownerType = ownerType;
    graph._rootInstances = instances;

    std::vector<reflection::PropertyAccessor::FLeaf> leaves;
    reflection::PropertyAccessor::collectLeaves(ownerType, instances, leaves);
    graph._nodes.reserve(leaves.size());
    for (reflection::PropertyAccessor::FLeaf& leaf : leaves) {
        if (!leaf.slot.property) {
            continue;
        }
        PropertyNode node;
        node.name = leaf.path;
        node.displayName = displayNameFromPath(leaf.path);
        if (leaf.slot.property && leaf.slot.property->metadata.hasMeta("category")) {
            try {
                node.category = leaf.slot.property->metadata.get<std::string>("category");
            }
            catch (...) {
            }
        }
        node.valueType = reflection::PropertyAccessor::valueType(leaf.slot);
        node.kind = leaf.role == reflection::PropertyAccessor::ELeafRole::Sequence
            ? PropertyNode::Kind::Sequence
            : leaf.role == reflection::PropertyAccessor::ELeafRole::Map
                ? PropertyNode::Kind::Map
                : PropertyNode::Kind::Value;
        node.binding = PropertyHandle(leaf.ownerType, std::move(leaf.ownerInstances), leaf.slot);
        node.bEditable = node.binding.isEditable();
        node.bVisible = true;
        node.bInstanceEditable = leaf.slot.property && leaf.slot.property->metadata.hasFlag(FieldFlags::InstanceEditable);
        node.bColor = leaf.slot.property && reflection::PropertyAccessor::isColor(*leaf.slot.property);
        graph._nodes.push_back(std::move(node));
    }
    return graph;
}

PropertyGraph PropertyGraph::project(type_index_t ownerType, std::vector<void*> instances)
{
    registerBuiltinPropertyProjections();
    PropertyGraph graph = build(ownerType, std::move(instances));
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
