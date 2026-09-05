#include "GameEditor/Inspector/PropertyGraphBuilder.h"

#include "Core/Reflection/PropertyExtensions.h"
#include "reflects-core/lib.h"

#include <algorithm>

namespace ya
{
namespace
{

using reflection::FPropertySlot;
using reflection::IContainerProperty;
using reflection::PropertyAccessor;
using ::Property;

void collectLeavesImpl(type_index_t currentType,
                       const std::vector<void*>& instances,
                       std::string_view pathPrefix,
                       std::vector<type_index_t>& ancestry,
                       const std::vector<FPropertySlot>& ownerPath,
                       const std::vector<void*>& rootInstances,
                       std::vector<PropertyGraphBuilder::FLeaf>& out)
{
    const ::Class* cls = ::ClassRegistry::instance().getClass(currentType);
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
        if (property.metadata.hasFlag(FieldFlags::NotSerialized) ||
            property.metadata.hasFlag(FieldFlags::Transient)) {
            continue;
        }

        const std::string leafPath = pathPrefix.empty()
            ? property.name
            : std::string(pathPrefix) + "." + property.name;

        if (PropertyAccessor::isCompositeType(property) &&
            std::find(ancestry.begin(), ancestry.end(), property.typeIndex) == ancestry.end()) {
            std::vector<void*> childInstances;
            childInstances.reserve(instances.size());
            bool allResolved = true;
            const FPropertySlot field(property);
            for (void* instance : instances) {
                void* child = PropertyAccessor::addressMutable(field, instance);
                if (!child) {
                    child = const_cast<void*>(PropertyAccessor::address(field, instance));
                }
                if (!child) {
                    allResolved = false;
                    break;
                }
                childInstances.push_back(child);
            }
            if (allResolved) {
                std::vector<FPropertySlot> childPath = ownerPath;
                childPath.push_back(field);
                collectLeavesImpl(property.typeIndex, childInstances, leafPath, ancestry, childPath, rootInstances, out);
                continue;
            }
        }

        if (PropertyAccessor::isMapOfLeaves(property)) {
            PropertyGraphBuilder::FLeaf header;
            header.ownerType = currentType;
            header.slot = FPropertySlot::field(property);
            header.path = leafPath;
            header.ownerInstances = instances;
            header.ownerPath = ownerPath;
            header.rootInstances = rootInstances;
            header.role = PropertyGraphBuilder::ELeafRole::Map;
            out.push_back(std::move(header));

            IContainerProperty* accessor = PropertyAccessor::containerOf(property);
            const FPropertySlot field(property);
            void* firstContainer = PropertyAccessor::addressMutable(field, instances.front());
            if (!firstContainer) {
                firstContainer = const_cast<void*>(PropertyAccessor::address(field, instances.front()));
            }
            if (accessor && firstContainer) {
                auto iterator = accessor->createIterator(firstContainer);
                while (iterator && iterator->hasNext()) {
                    const type_index_t keyType = iterator->getKeyTypeIndex();
                    void* keyPtr = iterator->getKeyPtr();
                    std::string key;
                    if (keyPtr && keyType == refl::type_index_v<std::string>) {
                        key = *static_cast<const std::string*>(keyPtr);
                    }
                    else if (keyPtr && (keyType == refl::type_index_v<int> || keyType == refl::type_index_v<int32_t>)) {
                        key = std::to_string(*static_cast<const int32_t*>(keyPtr));
                    }
                    else if (keyPtr && keyType == refl::type_index_v<uint32_t>) {
                        key = std::to_string(*static_cast<const uint32_t*>(keyPtr));
                    }
                    iterator->next();
                    if (key.empty() && keyType != refl::type_index_v<std::string>) {
                        continue;
                    }
                    const FPropertySlot valueSlot = FPropertySlot::at(property, key);
                    bool allPresent = true;
                    for (void* instance : instances) {
                        if (!PropertyAccessor::address(valueSlot, instance)) {
                            allPresent = false;
                            break;
                        }
                    }
                    if (!allPresent) {
                        continue;
                    }
                    PropertyGraphBuilder::FLeaf leaf;
                    leaf.ownerType = currentType;
                    leaf.slot = valueSlot;
                    leaf.path = keyType == refl::type_index_v<std::string>
                        ? leafPath + "[\"" + key + "\"]"
                        : leafPath + "[" + key + "]";
                    leaf.ownerInstances = instances;
                    leaf.ownerPath = ownerPath;
                    leaf.rootInstances = rootInstances;
                    out.push_back(std::move(leaf));
                }
            }
            continue;
        }

        if (PropertyAccessor::isSequenceOfLeaves(property)) {
            const FPropertySlot field(property);
            if (PropertyAccessor::isDynamicSequence(property)) {
                PropertyGraphBuilder::FLeaf header;
                header.ownerType = currentType;
                header.slot = field;
                header.path = leafPath;
                header.ownerInstances = instances;
                header.ownerPath = ownerPath;
                header.rootInstances = rootInstances;
                header.role = PropertyGraphBuilder::ELeafRole::Sequence;
                out.push_back(std::move(header));
            }
            size_t count = 0;
            bool sized = true;
            for (size_t index = 0; index < instances.size(); ++index) {
                void* container = PropertyAccessor::addressMutable(field, instances[index]);
                if (!container) {
                    container = const_cast<void*>(PropertyAccessor::address(field, instances[index]));
                }
                IContainerProperty* accessor = PropertyAccessor::containerOf(property);
                if (!container || !accessor) {
                    sized = false;
                    break;
                }
                const size_t size = accessor->getSize(container);
                count = index == 0 ? size : std::min(count, size);
            }
            if (sized) {
                for (size_t index = 0; index < count; ++index) {
                    PropertyGraphBuilder::FLeaf leaf;
                    leaf.ownerType = currentType;
                    leaf.slot = FPropertySlot::at(property, static_cast<int>(index));
                    leaf.path = leafPath + "[" + std::to_string(index) + "]";
                    leaf.ownerInstances = instances;
                    leaf.ownerPath = ownerPath;
                    leaf.rootInstances = rootInstances;
                    out.push_back(std::move(leaf));
                }
                continue;
            }
        }

        PropertyGraphBuilder::FLeaf leaf;
        leaf.ownerType = currentType;
        leaf.slot = FPropertySlot::field(property);
        leaf.path = leafPath;
        leaf.ownerInstances = instances;
        leaf.ownerPath = ownerPath;
        leaf.rootInstances = rootInstances;
        out.push_back(std::move(leaf));
    }
    ancestry.pop_back();
}

} // namespace

void PropertyGraphBuilder::collectLeaves(type_index_t rootType,
                                          const std::vector<void*>& roots,
                                          std::vector<FLeaf>& out)
{
    out.clear();
    std::vector<type_index_t> ancestry;
    collectLeavesImpl(rootType, roots, {}, ancestry, {}, roots, out);
}

} // namespace ya
