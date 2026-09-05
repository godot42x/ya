#pragma once

#include "Core/Api.h"
#include "Core/Reflection/MetadataSupport.h"
#include "Core/TypeIndex.h"

#include <glm/vec4.hpp>
#include <cstdint>
#include <string>
#include <string_view>
#include <optional>
#include <vector>

struct Property;

namespace ya::reflection
{
struct IContainerProperty;

/// Identifies one reflected value: a field, a sequence element, or a map value.
struct FPropertySlot
{
    const Property* property     = nullptr;
    int             elementIndex = -1;
    std::optional<std::string> mapKey;

    FPropertySlot() = default;
    FPropertySlot(const Property& field) : property(&field) {}

    static FPropertySlot field(const Property& property)
    {
        return FPropertySlot(property);
    }

    static FPropertySlot at(const Property& property, int index)
    {
        FPropertySlot slot(property);
        slot.elementIndex = index;
        return slot;
    }

    static FPropertySlot at(const Property& property, std::string key)
    {
        FPropertySlot slot(property);
        slot.mapKey.emplace(std::move(key));
        return slot;
    }

    [[nodiscard]] bool isValid() const { return property != nullptr; }
    [[nodiscard]] bool isField() const { return isValid() && elementIndex < 0 && !mapKey.has_value(); }
    [[nodiscard]] bool isSequenceElement() const { return isValid() && elementIndex >= 0 && !mapKey.has_value(); }
    [[nodiscard]] bool isMapValue() const { return isValid() && mapKey.has_value(); }
};

enum class EPropertyMutationStatus
{
    Invalid,
    ReadOnly,
    TypeMismatch,
    Unsupported,
    Unavailable,
    Unchanged,
    Changed,
};

struct FPropertyMutationResult
{
    EPropertyMutationStatus status = EPropertyMutationStatus::Invalid;

    [[nodiscard]] bool changed() const { return status == EPropertyMutationStatus::Changed; }
    [[nodiscard]] bool accepted() const
    {
        return status == EPropertyMutationStatus::Changed || status == EPropertyMutationStatus::Unchanged;
    }
};

/// Single-instance property access owned by the reflection layer.
/// Editor adapters (multi-select, undo, UI callbacks) sit on top of this.
struct YA_CORE_API PropertyAccessor
{
    [[nodiscard]] static bool isIntegerType(type_index_t typeIndex);
    [[nodiscard]] static bool isAssetRefType(type_index_t typeIndex);
    [[nodiscard]] static bool isLeafValueType(type_index_t typeIndex);
    [[nodiscard]] static bool isCompositeType(const Property& property);
    [[nodiscard]] static bool isSequenceOfLeaves(const Property& property);
    [[nodiscard]] static bool isDynamicSequence(const Property& property);
    [[nodiscard]] static bool isMapOfLeaves(const Property& property);
    [[nodiscard]] static bool isEditable(const Property& property);
    [[nodiscard]] static bool isEnum(const FPropertySlot& slot);
    [[nodiscard]] static IContainerProperty* containerOf(const Property& property);
    [[nodiscard]] static type_index_t valueType(const FPropertySlot& slot);

    [[nodiscard]] static const void* address(const FPropertySlot& slot, const void* instance);
    [[nodiscard]] static void* addressMutable(const FPropertySlot& slot, void* instance);

    [[nodiscard]] static size_t containerSize(const Property& property, const void* instance);
    [[nodiscard]] static FPropertyMutationResult appendEmptyResult(const Property& property, void* instance);
    static bool appendEmpty(const Property& property, void* instance);
    [[nodiscard]] static FPropertyMutationResult removeAtResult(const Property& property, void* instance, int index);
    static bool removeAt(const Property& property, void* instance, int index);
    [[nodiscard]] static FPropertyMutationResult insertEmptyAtResult(const Property& property, void* instance, int index);
    static bool insertEmptyAt(const Property& property, void* instance, int index);
    [[nodiscard]] static FPropertyMutationResult clearContainerResult(const Property& property, void* instance);
    static bool clearContainer(const Property& property, void* instance);
    [[nodiscard]] static FPropertyMutationResult removeMapKeyResult(const Property& property, void* instance, std::string_view key);
    static bool removeMapKey(const Property& property, void* instance, std::string_view key);
    [[nodiscard]] static FPropertyMutationResult insertMapKeyResult(const Property& property, void* instance, std::string_view key);
    static bool insertMapKey(const Property& property, void* instance, std::string_view key);

    [[nodiscard]] static bool equals(const FPropertySlot& slot, const void* a, const void* b);
    [[nodiscard]] static bool equalsVecAxis(const FPropertySlot& slot,
                                            const void* a,
                                            const void* b,
                                            int axis,
                                            int componentCount);

    template <typename T>
    [[nodiscard]] static bool tryGet(const FPropertySlot& slot, const void* instance, T& value)
    {
        if (!slot.property || valueType(slot) != ya::type_index_v<T>) {
            return false;
        }
        const void* addr = address(slot, instance);
        if (!addr) {
            return false;
        }
        value = *static_cast<const T*>(addr);
        return true;
    }

    template <typename T>
    [[nodiscard]] static FPropertyMutationResult setResult(const FPropertySlot& slot,
                                                           void* instance,
                                                           const T& value)
    {
        if (!slot.property) {
            return {EPropertyMutationStatus::Invalid};
        }
        if (valueType(slot) != ya::type_index_v<T>) {
            return {EPropertyMutationStatus::TypeMismatch};
        }
        if (!isEditable(*slot.property)) {
            return {EPropertyMutationStatus::ReadOnly};
        }
        void* addr = addressMutable(slot, instance);
        if (!addr) {
            return {EPropertyMutationStatus::Unavailable};
        }
        T& current = *static_cast<T*>(addr);
        if (current == value) {
            return {EPropertyMutationStatus::Unchanged};
        }
        current = value;
        return {EPropertyMutationStatus::Changed};
    }

    template <typename T>
    static bool set(const FPropertySlot& slot, void* instance, const T& value)
    {
        return setResult(slot, instance, value).changed();
    }

    [[nodiscard]] static bool tryGetInteger(const FPropertySlot& slot, const void* instance, int64_t& value);
    static bool setInteger(const FPropertySlot& slot, void* instance, int64_t value);

    [[nodiscard]] static bool tryGetEnumIndex(const FPropertySlot& slot, const void* instance, int& index);
    [[nodiscard]] static bool enumLabels(const FPropertySlot& slot, std::vector<std::string>& labels);
    static bool setEnumByIndex(const FPropertySlot& slot, void* instance, int index);
    [[nodiscard]] static bool tryGetEnumValue(const FPropertySlot& slot, const void* instance, int64_t& value);
    static bool setEnumValue(const FPropertySlot& slot, void* instance, int64_t value);

    [[nodiscard]] static bool tryGetColor(const FPropertySlot& slot, const void* instance, glm::vec4& value);
    static bool setColor(const FPropertySlot& slot, void* instance, const glm::vec4& value);

    [[nodiscard]] static bool tryGetAssetPath(const FPropertySlot& slot, const void* instance, std::string& value);
    static bool setAssetPath(const FPropertySlot& slot, void* instance, const std::string& value);
    [[nodiscard]] static bool hasAssetResolveError(const FPropertySlot& slot, const void* instance);


};

} // namespace ya::reflection
