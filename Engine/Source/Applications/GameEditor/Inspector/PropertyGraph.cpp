#include "GameEditor/Inspector/PropertyGraph.h"
#include "GameEditor/Inspector/PropertyProjection.h"

#include "Core/Common/AssetRef.h"
#include "Core/Reflection/MetadataSupport.h"
#include "reflects-core/lib.h"

#include <algorithm>
#include <cctype>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <string>
#include <vector>

namespace ya
{
namespace
{
std::string makeDisplayName(std::string_view name)
{
    while (!name.empty() && name.front() == '_') name.remove_prefix(1);
    std::string result;
    result.reserve(name.size());
    for (size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if (i > 0 && std::isupper(static_cast<unsigned char>(c)) && std::islower(static_cast<unsigned char>(name[i - 1]))) result.push_back(' ');
        result.push_back(i == 0 ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c);
    }
    return result;
}

bool isLeafEditableType(const PropertyNode& node)
{
    return node.valueType == refl::type_index_v<glm::vec2> ||
           node.valueType == refl::type_index_v<glm::vec3> ||
           node.valueType == refl::type_index_v<glm::vec4> ||
           node.valueType == refl::type_index_v<float> ||
           node.valueType == refl::type_index_v<int> ||
           node.valueType == refl::type_index_v<int32_t> ||
           node.valueType == refl::type_index_v<uint32_t> ||
           node.valueType == refl::type_index_v<bool> ||
           node.valueType == refl::type_index_v<std::string> ||
           node.binding.isAssetRef() ||
           EnumRegistry::instance().getEnum(node.valueType) != nullptr;
}

void appendGraphNodes(std::vector<PropertyNode>& outNodes,
                      type_index_t currentType,
                      const std::vector<void*>& instances,
                      std::string_view pathPrefix,
                      std::string_view displayPrefix,
                      std::vector<type_index_t>& ancestry)
{
    const Class* cls = ClassRegistry::instance().getClass(currentType);
    if (!cls || instances.empty()) {
        return;
    }
    ancestry.push_back(currentType);
    std::vector<std::string> names = cls->propertyOrder;
    for (const auto& [name, property] : cls->properties) {
        if (std::find(names.begin(), names.end(), name) == names.end()) {
            names.push_back(name);
        }
    }

    for (const std::string& name : names) {
        auto it = cls->properties.find(name);
        if (it == cls->properties.end()) {
            continue;
        }
        const Property& property = it->second;
        if (property.metadata.hasFlag(FieldFlags::NotSerialized) || property.metadata.hasFlag(FieldFlags::Transient)) {
            continue;
        }

        const std::string leafPath = pathPrefix.empty() ? property.name : std::string(pathPrefix) + "." + property.name;
        const std::string leafDisplay = displayPrefix.empty()
            ? makeDisplayName(property.name)
            : std::string(displayPrefix) + " / " + makeDisplayName(property.name);

        const bool bCompositeCandidate =
            property.addressGetter &&
            !EnumRegistry::instance().getEnum(property.typeIndex) &&
            property.typeIndex != refl::type_index_v<glm::vec2> &&
            property.typeIndex != refl::type_index_v<glm::vec3> &&
            property.typeIndex != refl::type_index_v<glm::vec4> &&
            property.typeIndex != refl::type_index_v<float> &&
            property.typeIndex != refl::type_index_v<int> &&
            property.typeIndex != refl::type_index_v<int32_t> &&
            property.typeIndex != refl::type_index_v<uint32_t> &&
            property.typeIndex != refl::type_index_v<bool> &&
            property.typeIndex != refl::type_index_v<std::string> &&
            !PropertyHandleFactory::make(currentType, instances, property.name).isAssetRef() &&
            ClassRegistry::instance().getClass(property.typeIndex) != nullptr &&
            std::find(ancestry.begin(), ancestry.end(), property.typeIndex) == ancestry.end();
        if (bCompositeCandidate) {
            std::vector<void*> childInstances;
            childInstances.reserve(instances.size());
            bool bAll = true;
            for (void* instance : instances) {
                void* child = property.addressGetterMutable ? property.addressGetterMutable(instance) : const_cast<void*>(property.addressGetter(instance));
                if (!child) {
                    bAll = false;
                    break;
                }
                childInstances.push_back(child);
            }
            if (bAll) {
                appendGraphNodes(outNodes, property.typeIndex, childInstances, leafPath, leafDisplay, ancestry);
                continue;
            }
        }

        PropertyNode node;
        node.name = leafPath;
        node.displayName = leafDisplay;
        if (property.metadata.hasMeta("category")) {
            try {
                node.category = property.metadata.get<std::string>("category");
            }
            catch (...) {
            }
        }
        node.valueType = property.typeIndex;
        node.binding = PropertyHandleFactory::make(currentType, instances, property.name);
        node.bEditable = node.binding.isEditable();
        node.bVisible = true;
        node.bInstanceEditable = property.metadata.hasFlag(FieldFlags::InstanceEditable);
        if (property.metadata.hasMeta(reflection::Meta::Color)) {
            try {
                node.bColor = property.metadata.get<bool>(reflection::Meta::Color);
            }
            catch (...) {
            }
        }
        outNodes.push_back(std::move(node));
    }
    ancestry.pop_back();
}
}

PropertyGraph PropertyGraph::build(type_index_t ownerType, std::vector<void*> instances)
{
    PropertyGraph graph;
    graph._ownerType = ownerType;
    graph._rootInstances = instances;
    std::vector<type_index_t> ancestry;
    appendGraphNodes(graph._nodes, ownerType, instances, {}, {}, ancestry);
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
