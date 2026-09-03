#include "GameEditor/Inspector/PropertyGraph.h"
#include "GameEditor/Inspector/PropertyProjection.h"

#include "Core/Common/AssetRef.h"
#include "Core/Reflection/MetadataSupport.h"
#include "reflects-core/lib.h"

#include <algorithm>
#include <cctype>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <string>

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
}

PropertyGraph PropertyGraph::build(type_index_t ownerType, std::vector<void*> instances)
{
    PropertyGraph graph;
    const Class* cls = ClassRegistry::instance().getClass(ownerType);
    if (!cls || instances.empty()) return graph;
    std::vector<std::string> names = cls->propertyOrder;
    for (const auto& [name, property] : cls->properties) {
        if (std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
    }
    graph._nodes.reserve(names.size());
    for (const std::string& name : names) {
        auto it = cls->properties.find(name);
        if (it == cls->properties.end()) continue;
        const Property& property = it->second;
        if (property.metadata.hasFlag(FieldFlags::NotSerialized) || property.metadata.hasFlag(FieldFlags::Transient)) continue;
        PropertyNode node;
        node.name = property.name;
        node.displayName = makeDisplayName(property.name);
        if (property.metadata.hasMeta("category")) {
            try { node.category = property.metadata.get<std::string>("category"); } catch (...) {}
        }
        node.valueType = property.typeIndex;
        node.binding = PropertyHandleFactory::make(ownerType, instances, property.name);
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
        if (!node.bVisible) {
            continue;
        }
        if (node.valueType == refl::type_index_v<glm::vec3> ||
            node.valueType == refl::type_index_v<float> ||
            node.valueType == refl::type_index_v<bool> ||
            node.valueType == refl::type_index_v<std::string> ||
            (node.bColor && node.valueType == refl::type_index_v<glm::vec4>) ||
            node.binding.isAssetRef() ||
            EnumRegistry::instance().getEnum(node.valueType) != nullptr) {
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
