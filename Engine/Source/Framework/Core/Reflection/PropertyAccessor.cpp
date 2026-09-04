#include "Core/Reflection/PropertyAccessor.h"

#include "Core/Common/AssetRef.h"
#include "Core/Reflection/PropertyExtensions.h"
#include "reflects-core/lib.h"

#include <algorithm>
#include <format>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <limits>

namespace ya::reflection
{
namespace
{

bool readManipulateSpec(const Property& property, Meta::ManipulateSpec& spec)
{
    if (!property.metadata.hasMeta(Meta::ManipulateSpec::name)) {
        return false;
    }
    try {
        spec = property.metadata.get<Meta::ManipulateSpec>(Meta::ManipulateSpec::name);
        return spec.type != Meta::ManipulateSpec::None;
    }
    catch (...) {
        return false;
    }
}

bool readIntegerFromAddress(type_index_t typeIndex, const void* address, int64_t& value)
{
    if (!address) {
        return false;
    }
    if (typeIndex == refl::type_index_v<int> || typeIndex == refl::type_index_v<int32_t>) {
        value = *static_cast<const int32_t*>(address);
        return true;
    }
    if (typeIndex == refl::type_index_v<uint32_t>) {
        value = *static_cast<const uint32_t*>(address);
        return true;
    }
    return false;
}

bool writeIntegerToAddress(type_index_t typeIndex, void* address, int64_t value)
{
    if (!address) {
        return false;
    }
    if (typeIndex == refl::type_index_v<int> || typeIndex == refl::type_index_v<int32_t>) {
        if (value < static_cast<int64_t>(std::numeric_limits<int32_t>::min()) ||
            value > static_cast<int64_t>(std::numeric_limits<int32_t>::max())) {
            return false;
        }
        int32_t& current = *static_cast<int32_t*>(address);
        const int32_t next = static_cast<int32_t>(value);
        if (current == next) {
            return false;
        }
        current = next;
        return true;
    }
    if (typeIndex == refl::type_index_v<uint32_t>) {
        if (value < 0 || value > static_cast<int64_t>(std::numeric_limits<uint32_t>::max())) {
            return false;
        }
        uint32_t& current = *static_cast<uint32_t*>(address);
        const uint32_t next = static_cast<uint32_t>(value);
        if (current == next) {
            return false;
        }
        current = next;
        return true;
    }
    return false;
}

void collectLeavesImpl(type_index_t currentType,
                       const std::vector<void*>& instances,
                       std::string_view pathPrefix,
                       std::vector<type_index_t>& ancestry,
                       std::vector<PropertyAccessor::FLeaf>& out)
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
            bool bAll = true;
            const FPropertySlot field(property);
            for (void* instance : instances) {
                void* child = PropertyAccessor::addressMutable(field, instance);
                if (!child) {
                    child = const_cast<void*>(PropertyAccessor::address(field, instance));
                }
                if (!child) {
                    bAll = false;
                    break;
                }
                childInstances.push_back(child);
            }
            if (bAll) {
                collectLeavesImpl(property.typeIndex, childInstances, leafPath, ancestry, out);
                continue;
            }
        }

        if (PropertyAccessor::isMapOfLeaves(property)) {
            PropertyAccessor::FLeaf header;
            header.ownerType = currentType;
            header.slot = FPropertySlot::field(property);
            header.path = leafPath;
            header.ownerInstances = instances;
            header.role = PropertyAccessor::ELeafRole::Map;
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
                    const std::string key = [&]() {
                        void* keyPtr = iterator->getKeyPtr();
                        if (!keyPtr) {
                            return std::string{};
                        }
                        if (keyType == refl::type_index_v<std::string>) {
                            return *static_cast<const std::string*>(keyPtr);
                        }
                        if (keyType == refl::type_index_v<int> || keyType == refl::type_index_v<int32_t>) {
                            return std::to_string(*static_cast<const int32_t*>(keyPtr));
                        }
                        if (keyType == refl::type_index_v<uint32_t>) {
                            return std::to_string(*static_cast<const uint32_t*>(keyPtr));
                        }
                        return std::string{};
                    }();
                    iterator->next();
                    if (key.empty() && keyType != refl::type_index_v<std::string>) {
                        continue;
                    }
                    const FPropertySlot valueSlot = FPropertySlot::at(property, key);
                    bool bAll = true;
                    for (void* instance : instances) {
                        if (!PropertyAccessor::address(valueSlot, instance)) {
                            bAll = false;
                            break;
                        }
                    }
                    if (!bAll) {
                        continue;
                    }
                    PropertyAccessor::FLeaf leaf;
                    leaf.ownerType = currentType;
                    leaf.slot = valueSlot;
                    leaf.path = keyType == refl::type_index_v<std::string>
                        ? leafPath + "[\"" + key + "\"]"
                        : leafPath + "[" + key + "]";
                    leaf.ownerInstances = instances;
                    leaf.role = PropertyAccessor::ELeafRole::Value;
                    out.push_back(std::move(leaf));
                }
            }
            continue;
        }

        if (PropertyAccessor::isSequenceOfLeaves(property)) {
            const FPropertySlot field(property);
            if (PropertyAccessor::isDynamicSequence(property)) {
                PropertyAccessor::FLeaf header;
                header.ownerType = currentType;
                header.slot = field;
                header.path = leafPath;
                header.ownerInstances = instances;
                header.role = PropertyAccessor::ELeafRole::Sequence;
                out.push_back(std::move(header));
            }
            size_t count = 0;
            bool bSized = true;
            for (size_t i = 0; i < instances.size(); ++i) {
                void* container = PropertyAccessor::addressMutable(field, instances[i]);
                if (!container) {
                    container = const_cast<void*>(PropertyAccessor::address(field, instances[i]));
                }
                IContainerProperty* accessor = PropertyAccessor::containerOf(property);
                if (!container || !accessor) {
                    bSized = false;
                    break;
                }
                const size_t size = accessor->getSize(container);
                if (i == 0) {
                    count = size;
                }
                else {
                    count = std::min(count, size);
                }
            }
            if (bSized) {
                for (size_t index = 0; index < count; ++index) {
                    PropertyAccessor::FLeaf leaf;
                    leaf.ownerType = currentType;
                    leaf.slot = FPropertySlot::at(property, static_cast<int>(index));
                    leaf.path = leafPath + "[" + std::to_string(index) + "]";
                    leaf.ownerInstances = instances;
                    out.push_back(std::move(leaf));
                }
                continue;
            }
        }

        PropertyAccessor::FLeaf leaf;
        leaf.ownerType = currentType;
        leaf.slot = FPropertySlot::field(property);
        leaf.path = leafPath;
        leaf.ownerInstances = instances;
        out.push_back(std::move(leaf));
    }
    ancestry.pop_back();
}

} // namespace

bool PropertyAccessor::isIntegerType(type_index_t typeIndex)
{
    return typeIndex == refl::type_index_v<int> ||
           typeIndex == refl::type_index_v<int32_t> ||
           typeIndex == refl::type_index_v<uint32_t>;
}

bool PropertyAccessor::isAssetRefType(type_index_t typeIndex)
{
    if (const IAssetRefResolver* resolver = getAssetRefResolver()) {
        return resolver->isAssetRefType(typeIndex);
    }
    return typeIndex == refl::type_index_v<TextureRef> ||
           typeIndex == refl::type_index_v<ModelRef> ||
           typeIndex == refl::type_index_v<MeshRef>;
}

bool PropertyAccessor::isLeafValueType(type_index_t typeIndex)
{
    return typeIndex == refl::type_index_v<glm::vec2> ||
           typeIndex == refl::type_index_v<glm::vec3> ||
           typeIndex == refl::type_index_v<glm::vec4> ||
           typeIndex == refl::type_index_v<float> ||
           isIntegerType(typeIndex) ||
           typeIndex == refl::type_index_v<bool> ||
           typeIndex == refl::type_index_v<std::string> ||
           isAssetRefType(typeIndex) ||
           EnumRegistry::instance().getEnum(typeIndex) != nullptr;
}

bool PropertyAccessor::isCompositeType(const Property& property)
{
    return property.addressGetter &&
           !isLeafValueType(property.typeIndex) &&
           containerOf(property) == nullptr &&
           ClassRegistry::instance().getClass(property.typeIndex) != nullptr;
}

bool PropertyAccessor::isSequenceOfLeaves(const Property& property)
{
    IContainerProperty* accessor = containerOf(property);
    if (!accessor || accessor->isMapLike()) {
        return false;
    }
    const EContainer type = accessor->getContainerType();
    if (type != EContainer::Vector && type != EContainer::Array) {
        return false;
    }
    return isLeafValueType(accessor->getElementTypeIndex());
}

bool PropertyAccessor::isDynamicSequence(const Property& property)
{
    IContainerProperty* accessor = containerOf(property);
    return accessor &&
           accessor->getContainerType() == EContainer::Vector &&
           isLeafValueType(accessor->getElementTypeIndex());
}

bool PropertyAccessor::isMapOfLeaves(const Property& property)
{
    IContainerProperty* accessor = containerOf(property);
    return accessor &&
           accessor->isMapLike() &&
           isLeafValueType(accessor->getElementTypeIndex()) &&
           isLeafValueType(accessor->getKeyTypeIndex());
}

IContainerProperty* PropertyAccessor::containerOf(const Property& property)
{
    const ContainerPropertyExtension* extension = PropertyContainerHelper::getContainerExtension(property);
    if (!extension || !extension->hasContainer()) {
        return nullptr;
    }
    return extension->containerAccessor.get();
}

type_index_t PropertyAccessor::valueType(const FPropertySlot& slot)
{
    if (!slot.property) {
        return 0;
    }
    if (slot.isSequenceElement() || slot.isMapValue()) {
        IContainerProperty* accessor = containerOf(*slot.property);
        return accessor ? accessor->getElementTypeIndex() : 0;
    }
    return slot.property->typeIndex;
}

bool PropertyAccessor::isEditable(const Property& property)
{
    return !property.bConst && property.addressGetterMutable &&
           !property.metadata.hasFlag(FieldFlags::EditReadOnly);
}

bool PropertyAccessor::isEnum(const FPropertySlot& slot)
{
    return EnumRegistry::instance().getEnum(valueType(slot)) != nullptr;
}

bool PropertyAccessor::isColor(const Property& property)
{
    if (!property.metadata.hasMeta(Meta::Color)) {
        return false;
    }
    try {
        return property.metadata.get<bool>(Meta::Color);
    }
    catch (...) {
        return false;
    }
}

const void* PropertyAccessor::address(const FPropertySlot& slot, const void* instance)
{
    if (!slot.property || !instance) {
        return nullptr;
    }
    if (slot.isField()) {
        if (!slot.property->addressGetter) {
            return nullptr;
        }
        return slot.property->addressGetter(instance);
    }
    IContainerProperty* accessor = containerOf(*slot.property);
    const void* container = address(FPropertySlot::field(*slot.property), instance);
    if (!accessor || !container) {
        return nullptr;
    }
    void* mutableContainer = const_cast<void*>(container);
    if (slot.isMapValue()) {
        return accessor->getValuePtr(mutableContainer, slot.mapKey);
    }
    if (static_cast<size_t>(slot.elementIndex) >= accessor->getSize(mutableContainer)) {
        return nullptr;
    }
    return accessor->getElementPtr(mutableContainer, static_cast<size_t>(slot.elementIndex));
}

void* PropertyAccessor::addressMutable(const FPropertySlot& slot, void* instance)
{
    if (!slot.property || !instance) {
        return nullptr;
    }
    if (slot.isField()) {
        if (!slot.property->addressGetterMutable) {
            return nullptr;
        }
        return slot.property->addressGetterMutable(instance);
    }
    IContainerProperty* accessor = containerOf(*slot.property);
    void* container = addressMutable(FPropertySlot::field(*slot.property), instance);
    if (!accessor || !container) {
        return nullptr;
    }
    if (slot.isMapValue()) {
        return accessor->getValuePtr(container, slot.mapKey);
    }
    if (static_cast<size_t>(slot.elementIndex) >= accessor->getSize(container)) {
        return nullptr;
    }
    return accessor->getElementPtr(container, static_cast<size_t>(slot.elementIndex));
}

namespace
{

void* containerPtr(const Property& property, void* instance)
{
    const FPropertySlot field(property);
    void* container = PropertyAccessor::addressMutable(field, instance);
    if (!container) {
        container = const_cast<void*>(PropertyAccessor::address(field, instance));
    }
    return container;
}

std::string mapKeyString(void* keyPtr, type_index_t keyType)
{
    if (!keyPtr) {
        return {};
    }
    if (keyType == refl::type_index_v<std::string>) {
        return *static_cast<const std::string*>(keyPtr);
    }
    if (keyType == refl::type_index_v<int> || keyType == refl::type_index_v<int32_t>) {
        return std::to_string(*static_cast<const int32_t*>(keyPtr));
    }
    if (keyType == refl::type_index_v<uint32_t>) {
        return std::to_string(*static_cast<const uint32_t*>(keyPtr));
    }
    if (keyType == refl::type_index_v<bool>) {
        return *static_cast<const bool*>(keyPtr) ? "true" : "false";
    }
    return {};
}

std::string mapLeafPath(std::string_view prefix, std::string_view key, type_index_t keyType)
{
    if (keyType == refl::type_index_v<std::string>) {
        return std::string(prefix) + "[\"" + std::string(key) + "\"]";
    }
    return std::string(prefix) + "[" + std::string(key) + "]";
}

} // namespace

size_t PropertyAccessor::containerSize(const Property& property, const void* instance)
{
    IContainerProperty* accessor = containerOf(property);
    void* container = const_cast<void*>(address(FPropertySlot::field(property), instance));
    if (!accessor || !container) {
        return 0;
    }
    return accessor->getSize(container);
}

bool PropertyAccessor::appendEmpty(const Property& property, void* instance)
{
    IContainerProperty* accessor = containerOf(property);
    void* container = containerPtr(property, instance);
    if (!isEditable(property) || !accessor || !container || accessor->isFixedSize()) {
        return false;
    }
    const size_t before = accessor->getSize(container);
    accessor->addEmptyEntry(container);
    return accessor->getSize(container) > before;
}

bool PropertyAccessor::removeAt(const Property& property, void* instance, int index)
{
    IContainerProperty* accessor = containerOf(property);
    void* container = containerPtr(property, instance);
    if (!isEditable(property) || !accessor || !container || accessor->isFixedSize() || index < 0) {
        return false;
    }
    if (static_cast<size_t>(index) >= accessor->getSize(container)) {
        return false;
    }
    accessor->removeElement(container, static_cast<size_t>(index));
    return true;
}

bool PropertyAccessor::insertEmptyAt(const Property& property, void* instance, int index)
{
    IContainerProperty* accessor = containerOf(property);
    void* container = containerPtr(property, instance);
    if (!isEditable(property) || !accessor || !container || accessor->isFixedSize() || index < 0) {
        return false;
    }
    const size_t before = accessor->getSize(container);
    accessor->insertEmptyAt(container, static_cast<size_t>(index));
    return accessor->getSize(container) > before;
}

bool PropertyAccessor::clearContainer(const Property& property, void* instance)
{
    IContainerProperty* accessor = containerOf(property);
    void* container = containerPtr(property, instance);
    if (!isEditable(property) || !accessor || !container || accessor->isFixedSize()) {
        return false;
    }
    if (accessor->getSize(container) == 0) {
        return false;
    }
    accessor->clear(container);
    return true;
}

bool PropertyAccessor::removeMapKey(const Property& property, void* instance, std::string_view key)
{
    IContainerProperty* accessor = containerOf(property);
    void* container = containerPtr(property, instance);
    if (!isEditable(property) || !accessor || !container || !accessor->isMapLike() || key.empty()) {
        return false;
    }
    const std::string owned(key);
    void* value = accessor->getValuePtr(container, owned);
    if (!value) {
        return false;
    }
    if (accessor->getKeyTypeIndex() == refl::type_index_v<std::string>) {
        accessor->removeByKey(container, const_cast<std::string*>(&owned));
        return true;
    }
    if (accessor->getKeyTypeIndex() == refl::type_index_v<int> ||
        accessor->getKeyTypeIndex() == refl::type_index_v<int32_t>) {
        int32_t parsed = 0;
        try {
            parsed = static_cast<int32_t>(std::stoi(owned));
        }
        catch (...) {
            return false;
        }
        accessor->removeByKey(container, &parsed);
        return true;
    }
    return false;
}

bool PropertyAccessor::insertMapKey(const Property& property, void* instance, std::string_view key)
{
    IContainerProperty* accessor = containerOf(property);
    void* container = containerPtr(property, instance);
    if (!isEditable(property) || !accessor || !container || !accessor->isMapLike()) {
        return false;
    }
    if (accessor->getKeyTypeIndex() != refl::type_index_v<std::string>) {
        return false;
    }
    std::string owned(key);
    if (accessor->getValuePtr(container, owned)) {
        return false;
    }
    accessor->insertElement(container, &owned, nullptr);
    return accessor->getValuePtr(container, owned) != nullptr;
}

bool PropertyAccessor::equals(const FPropertySlot& slot, const void* a, const void* b)
{
    if (!a || !b) {
        return false;
    }
    const type_index_t type = valueType(slot);
    if (const Enum* enumInfo = EnumRegistry::instance().getEnum(type)) {
        return enumInfo->getValue(const_cast<void*>(a)) == enumInfo->getValue(const_cast<void*>(b));
    }
    if (type == refl::type_index_v<glm::vec2>) {
        return *static_cast<const glm::vec2*>(a) == *static_cast<const glm::vec2*>(b);
    }
    if (type == refl::type_index_v<glm::vec3>) {
        return *static_cast<const glm::vec3*>(a) == *static_cast<const glm::vec3*>(b);
    }
    if (type == refl::type_index_v<glm::vec4>) {
        return *static_cast<const glm::vec4*>(a) == *static_cast<const glm::vec4*>(b);
    }
    if (type == refl::type_index_v<float>) {
        return *static_cast<const float*>(a) == *static_cast<const float*>(b);
    }
    if (isIntegerType(type)) {
        int64_t left = 0;
        int64_t right = 0;
        return readIntegerFromAddress(type, a, left) &&
               readIntegerFromAddress(type, b, right) &&
               left == right;
    }
    if (type == refl::type_index_v<bool>) {
        return *static_cast<const bool*>(a) == *static_cast<const bool*>(b);
    }
    if (type == refl::type_index_v<std::string>) {
        return *static_cast<const std::string*>(a) == *static_cast<const std::string*>(b);
    }
    if (isAssetRefType(type)) {
        return static_cast<const AssetRefBase*>(a)->getPath() == static_cast<const AssetRefBase*>(b)->getPath();
    }
    return false;
}

bool PropertyAccessor::equalsVecAxis(const FPropertySlot& slot,
                                     const void* a,
                                     const void* b,
                                     int axis,
                                     int componentCount)
{
    if (!a || !b || axis < 0 || axis >= componentCount) {
        return false;
    }
    const type_index_t type = valueType(slot);
    if (componentCount == 2 && type == refl::type_index_v<glm::vec2>) {
        return (*static_cast<const glm::vec2*>(a))[axis] == (*static_cast<const glm::vec2*>(b))[axis];
    }
    if (componentCount == 3 && type == refl::type_index_v<glm::vec3>) {
        return (*static_cast<const glm::vec3*>(a))[axis] == (*static_cast<const glm::vec3*>(b))[axis];
    }
    if (componentCount == 4 && type == refl::type_index_v<glm::vec4>) {
        return (*static_cast<const glm::vec4*>(a))[axis] == (*static_cast<const glm::vec4*>(b))[axis];
    }
    return false;
}

bool PropertyAccessor::tryGetInteger(const FPropertySlot& slot, const void* instance, int64_t& value)
{
    if (!isIntegerType(valueType(slot))) {
        return false;
    }
    return readIntegerFromAddress(valueType(slot), address(slot, instance), value);
}

bool PropertyAccessor::setInteger(const FPropertySlot& slot, void* instance, int64_t value)
{
    if (!slot.property || !isEditable(*slot.property) || !isIntegerType(valueType(slot))) {
        return false;
    }
    return writeIntegerToAddress(valueType(slot), addressMutable(slot, instance), value);
}

bool PropertyAccessor::tryGetEnumIndex(const FPropertySlot& slot, const void* instance, int& index)
{
    index = -1;
    const Enum* enumInfo = EnumRegistry::instance().getEnum(valueType(slot));
    const void* addr = address(slot, instance);
    if (!enumInfo || !addr) {
        return false;
    }
    const int64_t value = enumInfo->getValue(const_cast<void*>(addr));
    for (size_t i = 0; i < enumInfo->values.size(); ++i) {
        if (enumInfo->values[i].value == value) {
            index = static_cast<int>(i);
            return true;
        }
    }
    return false;
}

bool PropertyAccessor::enumLabels(const FPropertySlot& slot, std::vector<std::string>& labels)
{
    labels.clear();
    const Enum* enumInfo = EnumRegistry::instance().getEnum(valueType(slot));
    if (!enumInfo) {
        return false;
    }
    labels.reserve(enumInfo->values.size());
    for (const EnumValue& entry : enumInfo->values) {
        labels.push_back(entry.name);
    }
    return true;
}

bool PropertyAccessor::setEnumByIndex(const FPropertySlot& slot, void* instance, int index)
{
    const Enum* enumInfo = EnumRegistry::instance().getEnum(valueType(slot));
    if (!slot.property || !isEditable(*slot.property) || !enumInfo ||
        index < 0 || index >= static_cast<int>(enumInfo->values.size())) {
        return false;
    }
    return setEnumValue(slot, instance, enumInfo->values[static_cast<size_t>(index)].value);
}

bool PropertyAccessor::tryGetEnumValue(const FPropertySlot& slot, const void* instance, int64_t& value)
{
    const Enum* enumInfo = EnumRegistry::instance().getEnum(valueType(slot));
    const void* addr = address(slot, instance);
    if (!enumInfo || !addr) {
        return false;
    }
    value = enumInfo->getValue(const_cast<void*>(addr));
    return true;
}

bool PropertyAccessor::setEnumValue(const FPropertySlot& slot, void* instance, int64_t value)
{
    const Enum* enumInfo = EnumRegistry::instance().getEnum(valueType(slot));
    void* addr = addressMutable(slot, instance);
    if (!slot.property || !isEditable(*slot.property) || !enumInfo || !addr) {
        return false;
    }
    if (enumInfo->getValue(addr) == value) {
        return false;
    }
    enumInfo->setValue(addr, value);
    return true;
}

bool PropertyAccessor::tryGetColor(const FPropertySlot& slot, const void* instance, glm::vec4& value)
{
    if (!slot.property || !isColor(*slot.property)) {
        return false;
    }
    const void* addr = address(slot, instance);
    if (!addr) {
        return false;
    }
    const type_index_t type = valueType(slot);
    if (type == refl::type_index_v<glm::vec4>) {
        value = *static_cast<const glm::vec4*>(addr);
        return true;
    }
    if (type == refl::type_index_v<glm::vec3>) {
        value = glm::vec4(*static_cast<const glm::vec3*>(addr), 1.0f);
        return true;
    }
    return false;
}

bool PropertyAccessor::setColor(const FPropertySlot& slot, void* instance, const glm::vec4& value)
{
    if (!slot.property || !isEditable(*slot.property) || !isColor(*slot.property)) {
        return false;
    }
    void* addr = addressMutable(slot, instance);
    if (!addr) {
        return false;
    }
    const type_index_t type = valueType(slot);
    if (type == refl::type_index_v<glm::vec4>) {
        auto& current = *static_cast<glm::vec4*>(addr);
        if (current == value) {
            return false;
        }
        current = value;
        return true;
    }
    if (type == refl::type_index_v<glm::vec3>) {
        const glm::vec3 next(value);
        auto& current = *static_cast<glm::vec3*>(addr);
        if (current == next) {
            return false;
        }
        current = next;
        return true;
    }
    return false;
}

bool PropertyAccessor::tryGetAssetPath(const FPropertySlot& slot, const void* instance, std::string& value)
{
    if (!isAssetRefType(valueType(slot))) {
        return false;
    }
    const void* addr = address(slot, instance);
    if (!addr) {
        return false;
    }
    value = static_cast<const AssetRefBase*>(addr)->getPath();
    return true;
}

bool PropertyAccessor::setAssetPath(const FPropertySlot& slot, void* instance, const std::string& value)
{
    if (!slot.property || !isEditable(*slot.property) || !isAssetRefType(valueType(slot))) {
        return false;
    }
    void* addr = addressMutable(slot, instance);
    if (!addr) {
        return false;
    }
    auto* ref = static_cast<AssetRefBase*>(addr);
    if (ref->getPath() == value) {
        return false;
    }
    ref->setPath(value);
    return true;
}

bool PropertyAccessor::hasAssetResolveError(const FPropertySlot& slot, const void* instance)
{
    const type_index_t type = valueType(slot);
    if (!isAssetRefType(type)) {
        return false;
    }
    const void* addr = address(slot, instance);
    if (!addr) {
        return false;
    }
    const auto* ref = static_cast<const AssetRefBase*>(addr);
    if (!ref->hasPath()) {
        return false;
    }
    if (type == refl::type_index_v<TextureRef>) {
        return static_cast<const TextureRef*>(ref)->getResolveState() == EAssetResolveState::Failed;
    }
    if (type == refl::type_index_v<ModelRef>) {
        return static_cast<const ModelRef*>(ref)->getResolveState() == EAssetResolveState::Failed;
    }
    if (type == refl::type_index_v<MeshRef>) {
        return static_cast<const MeshRef*>(ref)->getResolveState() == EAssetResolveState::Failed;
    }
    return false;
}

bool PropertyAccessor::tryGetManipulateSpec(const Property& property, Meta::ManipulateSpec& spec)
{
    return readManipulateSpec(property, spec);
}

std::string PropertyAccessor::validationError(const FPropertySlot& slot, const void* instance)
{
    if (!slot.property) {
        return {};
    }
    Meta::ManipulateSpec spec;
    if (!readManipulateSpec(*slot.property, spec)) {
        return {};
    }
    auto outOfRange = [&](double value) {
        return value < spec.min || value > spec.max;
    };
    const type_index_t type = valueType(slot);
    if (type == refl::type_index_v<float>) {
        float value = 0.0f;
        if (!tryGet(slot, instance, value)) {
            return "Invalid value";
        }
        if (outOfRange(value)) {
            return std::format("Value must be between {} and {}", spec.min, spec.max);
        }
        return {};
    }
    if (isIntegerType(type)) {
        int64_t value = 0;
        if (!tryGetInteger(slot, instance, value)) {
            return "Invalid value";
        }
        if (outOfRange(static_cast<double>(value))) {
            return std::format("Value must be between {} and {}", spec.min, spec.max);
        }
        return {};
    }
    if (type == refl::type_index_v<glm::vec2>) {
        glm::vec2 value{};
        if (!tryGet(slot, instance, value)) {
            return "Invalid value";
        }
        for (int axis = 0; axis < 2; ++axis) {
            if (outOfRange(value[axis])) {
                return std::format("Component must be between {} and {}", spec.min, spec.max);
            }
        }
        return {};
    }
    if (type == refl::type_index_v<glm::vec3>) {
        glm::vec3 value{};
        if (!tryGet(slot, instance, value)) {
            return "Invalid value";
        }
        for (int axis = 0; axis < 3; ++axis) {
            if (outOfRange(value[axis])) {
                return std::format("Component must be between {} and {}", spec.min, spec.max);
            }
        }
        return {};
    }
    if (type == refl::type_index_v<glm::vec4>) {
        glm::vec4 value{};
        if (!tryGet(slot, instance, value)) {
            return "Invalid value";
        }
        for (int axis = 0; axis < 4; ++axis) {
            if (outOfRange(value[axis])) {
                return std::format("Component must be between {} and {}", spec.min, spec.max);
            }
        }
    }
    return {};
}

void PropertyAccessor::collectLeaves(type_index_t rootType,
                                     const std::vector<void*>& roots,
                                     std::vector<FLeaf>& out)
{
    out.clear();
    std::vector<type_index_t> ancestry;
    collectLeavesImpl(rootType, roots, {}, ancestry, out);
}

} // namespace ya::reflection
