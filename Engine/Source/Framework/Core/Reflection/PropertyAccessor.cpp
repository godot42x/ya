#include "Core/Reflection/PropertyAccessor.h"

#include "Core/Common/AssetRef.h"
#include "reflects-core/lib.h"

#include <algorithm>
#include <format>
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

template <typename T>
bool tryGetPod(const Property& property, const void* instance, T& value)
{
    if (property.typeIndex != refl::type_index_v<T>) {
        return false;
    }
    const void* addr = PropertyAccessor::address(property, instance);
    if (!addr) {
        return false;
    }
    value = *static_cast<const T*>(addr);
    return true;
}

template <typename T>
bool setPod(const Property& property, void* instance, const T& value)
{
    if (!PropertyAccessor::isEditable(property) || property.typeIndex != refl::type_index_v<T>) {
        return false;
    }
    void* addr = PropertyAccessor::addressMutable(property, instance);
    if (!addr) {
        return false;
    }
    T& current = *static_cast<T*>(addr);
    if (current == value) {
        return false;
    }
    current = value;
    return true;
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
            for (void* instance : instances) {
                void* child = PropertyAccessor::addressMutable(property, instance);
                if (!child) {
                    child = const_cast<void*>(PropertyAccessor::address(property, instance));
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

        PropertyAccessor::FLeaf leaf;
        leaf.ownerType = currentType;
        leaf.property = &property;
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
           ClassRegistry::instance().getClass(property.typeIndex) != nullptr;
}

bool PropertyAccessor::isEditable(const Property& property)
{
    return !property.bConst && property.addressGetterMutable &&
           !property.metadata.hasFlag(FieldFlags::EditReadOnly);
}

bool PropertyAccessor::isEnum(const Property& property)
{
    return EnumRegistry::instance().getEnum(property.typeIndex) != nullptr;
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

const void* PropertyAccessor::address(const Property& property, const void* instance)
{
    if (!instance || !property.addressGetter) {
        return nullptr;
    }
    return property.addressGetter(instance);
}

void* PropertyAccessor::addressMutable(const Property& property, void* instance)
{
    if (!instance || !property.addressGetterMutable) {
        return nullptr;
    }
    return property.addressGetterMutable(instance);
}

bool PropertyAccessor::equals(const Property& property, const void* a, const void* b)
{
    if (!a || !b) {
        return false;
    }
    if (const Enum* enumInfo = EnumRegistry::instance().getEnum(property.typeIndex)) {
        return enumInfo->getValue(const_cast<void*>(a)) == enumInfo->getValue(const_cast<void*>(b));
    }
    if (property.typeIndex == refl::type_index_v<glm::vec2>) {
        return *static_cast<const glm::vec2*>(a) == *static_cast<const glm::vec2*>(b);
    }
    if (property.typeIndex == refl::type_index_v<glm::vec3>) {
        return *static_cast<const glm::vec3*>(a) == *static_cast<const glm::vec3*>(b);
    }
    if (property.typeIndex == refl::type_index_v<glm::vec4>) {
        return *static_cast<const glm::vec4*>(a) == *static_cast<const glm::vec4*>(b);
    }
    if (property.typeIndex == refl::type_index_v<float>) {
        return *static_cast<const float*>(a) == *static_cast<const float*>(b);
    }
    if (isIntegerType(property.typeIndex)) {
        int64_t left = 0;
        int64_t right = 0;
        return readIntegerFromAddress(property.typeIndex, a, left) &&
               readIntegerFromAddress(property.typeIndex, b, right) &&
               left == right;
    }
    if (property.typeIndex == refl::type_index_v<bool>) {
        return *static_cast<const bool*>(a) == *static_cast<const bool*>(b);
    }
    if (property.typeIndex == refl::type_index_v<std::string>) {
        return *static_cast<const std::string*>(a) == *static_cast<const std::string*>(b);
    }
    if (isAssetRefType(property.typeIndex)) {
        return static_cast<const AssetRefBase*>(a)->getPath() == static_cast<const AssetRefBase*>(b)->getPath();
    }
    return false;
}

bool PropertyAccessor::equalsVecAxis(const Property& property,
                                     const void* a,
                                     const void* b,
                                     int axis,
                                     int componentCount)
{
    if (!a || !b || axis < 0 || axis >= componentCount) {
        return false;
    }
    if (componentCount == 2 && property.typeIndex == refl::type_index_v<glm::vec2>) {
        return (*static_cast<const glm::vec2*>(a))[axis] == (*static_cast<const glm::vec2*>(b))[axis];
    }
    if (componentCount == 3 && property.typeIndex == refl::type_index_v<glm::vec3>) {
        return (*static_cast<const glm::vec3*>(a))[axis] == (*static_cast<const glm::vec3*>(b))[axis];
    }
    if (componentCount == 4 && property.typeIndex == refl::type_index_v<glm::vec4>) {
        return (*static_cast<const glm::vec4*>(a))[axis] == (*static_cast<const glm::vec4*>(b))[axis];
    }
    return false;
}

bool PropertyAccessor::tryGetVec2(const Property& property, const void* instance, glm::vec2& value)
{
    return tryGetPod(property, instance, value);
}

bool PropertyAccessor::setVec2(const Property& property, void* instance, const glm::vec2& value)
{
    return setPod(property, instance, value);
}

bool PropertyAccessor::tryGetVec3(const Property& property, const void* instance, glm::vec3& value)
{
    return tryGetPod(property, instance, value);
}

bool PropertyAccessor::setVec3(const Property& property, void* instance, const glm::vec3& value)
{
    return setPod(property, instance, value);
}

bool PropertyAccessor::tryGetVec4(const Property& property, const void* instance, glm::vec4& value)
{
    return tryGetPod(property, instance, value);
}

bool PropertyAccessor::setVec4(const Property& property, void* instance, const glm::vec4& value)
{
    return setPod(property, instance, value);
}

bool PropertyAccessor::tryGetFloat(const Property& property, const void* instance, float& value)
{
    return tryGetPod(property, instance, value);
}

bool PropertyAccessor::setFloat(const Property& property, void* instance, float value)
{
    return setPod(property, instance, value);
}

bool PropertyAccessor::tryGetInteger(const Property& property, const void* instance, int64_t& value)
{
    if (!isIntegerType(property.typeIndex)) {
        return false;
    }
    return readIntegerFromAddress(property.typeIndex, address(property, instance), value);
}

bool PropertyAccessor::setInteger(const Property& property, void* instance, int64_t value)
{
    if (!isEditable(property) || !isIntegerType(property.typeIndex)) {
        return false;
    }
    return writeIntegerToAddress(property.typeIndex, addressMutable(property, instance), value);
}

bool PropertyAccessor::tryGetBool(const Property& property, const void* instance, bool& value)
{
    return tryGetPod(property, instance, value);
}

bool PropertyAccessor::setBool(const Property& property, void* instance, bool value)
{
    return setPod(property, instance, value);
}

bool PropertyAccessor::tryGetString(const Property& property, const void* instance, std::string& value)
{
    return tryGetPod(property, instance, value);
}

bool PropertyAccessor::setString(const Property& property, void* instance, const std::string& value)
{
    return setPod(property, instance, value);
}

bool PropertyAccessor::tryGetEnumIndex(const Property& property, const void* instance, int& index)
{
    index = -1;
    const Enum* enumInfo = EnumRegistry::instance().getEnum(property.typeIndex);
    const void* addr = address(property, instance);
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

bool PropertyAccessor::enumLabels(const Property& property, std::vector<std::string>& labels)
{
    labels.clear();
    const Enum* enumInfo = EnumRegistry::instance().getEnum(property.typeIndex);
    if (!enumInfo) {
        return false;
    }
    labels.reserve(enumInfo->values.size());
    for (const EnumValue& entry : enumInfo->values) {
        labels.push_back(entry.name);
    }
    return true;
}

bool PropertyAccessor::setEnumByIndex(const Property& property, void* instance, int index)
{
    const Enum* enumInfo = EnumRegistry::instance().getEnum(property.typeIndex);
    if (!isEditable(property) || !enumInfo || index < 0 || index >= static_cast<int>(enumInfo->values.size())) {
        return false;
    }
    return setEnumValue(property, instance, enumInfo->values[static_cast<size_t>(index)].value);
}

bool PropertyAccessor::tryGetEnumValue(const Property& property, const void* instance, int64_t& value)
{
    const Enum* enumInfo = EnumRegistry::instance().getEnum(property.typeIndex);
    const void* addr = address(property, instance);
    if (!enumInfo || !addr) {
        return false;
    }
    value = enumInfo->getValue(const_cast<void*>(addr));
    return true;
}

bool PropertyAccessor::setEnumValue(const Property& property, void* instance, int64_t value)
{
    const Enum* enumInfo = EnumRegistry::instance().getEnum(property.typeIndex);
    void* addr = addressMutable(property, instance);
    if (!isEditable(property) || !enumInfo || !addr) {
        return false;
    }
    if (enumInfo->getValue(addr) == value) {
        return false;
    }
    enumInfo->setValue(addr, value);
    return true;
}

bool PropertyAccessor::tryGetColor(const Property& property, const void* instance, glm::vec4& value)
{
    if (!isColor(property)) {
        return false;
    }
    const void* addr = address(property, instance);
    if (!addr) {
        return false;
    }
    if (property.typeIndex == refl::type_index_v<glm::vec4>) {
        value = *static_cast<const glm::vec4*>(addr);
        return true;
    }
    if (property.typeIndex == refl::type_index_v<glm::vec3>) {
        value = glm::vec4(*static_cast<const glm::vec3*>(addr), 1.0f);
        return true;
    }
    return false;
}

bool PropertyAccessor::setColor(const Property& property, void* instance, const glm::vec4& value)
{
    if (!isEditable(property) || !isColor(property)) {
        return false;
    }
    void* addr = addressMutable(property, instance);
    if (!addr) {
        return false;
    }
    if (property.typeIndex == refl::type_index_v<glm::vec4>) {
        auto& current = *static_cast<glm::vec4*>(addr);
        if (current == value) {
            return false;
        }
        current = value;
        return true;
    }
    if (property.typeIndex == refl::type_index_v<glm::vec3>) {
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

bool PropertyAccessor::tryGetAssetPath(const Property& property, const void* instance, std::string& value)
{
    if (!isAssetRefType(property.typeIndex)) {
        return false;
    }
    const void* addr = address(property, instance);
    if (!addr) {
        return false;
    }
    value = static_cast<const AssetRefBase*>(addr)->getPath();
    return true;
}

bool PropertyAccessor::setAssetPath(const Property& property, void* instance, const std::string& value)
{
    if (!isEditable(property) || !isAssetRefType(property.typeIndex)) {
        return false;
    }
    void* addr = addressMutable(property, instance);
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

bool PropertyAccessor::hasAssetResolveError(const Property& property, const void* instance)
{
    if (!isAssetRefType(property.typeIndex)) {
        return false;
    }
    const void* addr = address(property, instance);
    if (!addr) {
        return false;
    }
    const auto* ref = static_cast<const AssetRefBase*>(addr);
    if (!ref->hasPath()) {
        return false;
    }
    if (property.typeIndex == refl::type_index_v<TextureRef>) {
        return static_cast<const TextureRef*>(ref)->getResolveState() == EAssetResolveState::Failed;
    }
    if (property.typeIndex == refl::type_index_v<ModelRef>) {
        return static_cast<const ModelRef*>(ref)->getResolveState() == EAssetResolveState::Failed;
    }
    if (property.typeIndex == refl::type_index_v<MeshRef>) {
        return static_cast<const MeshRef*>(ref)->getResolveState() == EAssetResolveState::Failed;
    }
    return false;
}

bool PropertyAccessor::tryGetManipulateSpec(const Property& property, Meta::ManipulateSpec& spec)
{
    return readManipulateSpec(property, spec);
}

std::string PropertyAccessor::validationError(const Property& property, const void* instance)
{
    Meta::ManipulateSpec spec;
    if (!readManipulateSpec(property, spec)) {
        return {};
    }
    auto outOfRange = [&](double value) {
        return value < spec.min || value > spec.max;
    };
    if (property.typeIndex == refl::type_index_v<float>) {
        float value = 0.0f;
        if (!tryGetFloat(property, instance, value)) {
            return "Invalid value";
        }
        if (outOfRange(value)) {
            return std::format("Value must be between {} and {}", spec.min, spec.max);
        }
        return {};
    }
    if (isIntegerType(property.typeIndex)) {
        int64_t value = 0;
        if (!tryGetInteger(property, instance, value)) {
            return "Invalid value";
        }
        if (outOfRange(static_cast<double>(value))) {
            return std::format("Value must be between {} and {}", spec.min, spec.max);
        }
        return {};
    }
    if (property.typeIndex == refl::type_index_v<glm::vec2>) {
        glm::vec2 value{};
        if (!tryGetVec2(property, instance, value)) {
            return "Invalid value";
        }
        for (int axis = 0; axis < 2; ++axis) {
            if (outOfRange(value[axis])) {
                return std::format("Component must be between {} and {}", spec.min, spec.max);
            }
        }
        return {};
    }
    if (property.typeIndex == refl::type_index_v<glm::vec3>) {
        glm::vec3 value{};
        if (!tryGetVec3(property, instance, value)) {
            return "Invalid value";
        }
        for (int axis = 0; axis < 3; ++axis) {
            if (outOfRange(value[axis])) {
                return std::format("Component must be between {} and {}", spec.min, spec.max);
            }
        }
        return {};
    }
    if (property.typeIndex == refl::type_index_v<glm::vec4>) {
        glm::vec4 value{};
        if (!tryGetVec4(property, instance, value)) {
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
