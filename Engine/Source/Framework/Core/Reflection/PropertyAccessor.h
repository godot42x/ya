#pragma once

#include "Core/Api.h"
#include "Core/Reflection/MetadataSupport.h"
#include "Core/TypeIndex.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct Property;

namespace ya::reflection
{
struct IContainerProperty;

/// Identifies a reflected value: the property itself, a sequence element, or a map value.
struct FValueLoc
{
    int         elementIndex = -1;
    std::string mapKey;

    FValueLoc() = default;
    FValueLoc(int index) : elementIndex(index) {}
    FValueLoc(int index, std::string key) : elementIndex(index), mapKey(std::move(key)) {}
};

/// Single-instance property access owned by the reflection layer.
/// Editor adapters (multi-select, undo, UI callbacks) sit on top of this.
struct YA_CORE_API PropertyAccessor
{
    enum class ELeafRole
    {
        Value,
        Sequence,
        Map,
    };

    struct FLeaf
    {
        type_index_t         ownerType = 0;
        const Property*      property  = nullptr;
        std::string          path;
        std::vector<void*>   ownerInstances;
        FValueLoc            loc;
        ELeafRole            role = ELeafRole::Value;
    };

    [[nodiscard]] static bool isIntegerType(type_index_t typeIndex);
    [[nodiscard]] static bool isAssetRefType(type_index_t typeIndex);
    [[nodiscard]] static bool isLeafValueType(type_index_t typeIndex);
    [[nodiscard]] static bool isCompositeType(const Property& property);
    [[nodiscard]] static bool isSequenceOfLeaves(const Property& property);
    [[nodiscard]] static bool isDynamicSequence(const Property& property);
    [[nodiscard]] static bool isMapOfLeaves(const Property& property);
    [[nodiscard]] static bool isEditable(const Property& property);
    [[nodiscard]] static bool isEnum(const Property& property, const FValueLoc& loc = {});
    [[nodiscard]] static bool isColor(const Property& property);
    [[nodiscard]] static IContainerProperty* containerOf(const Property& property);
    [[nodiscard]] static type_index_t valueType(const Property& property, const FValueLoc& loc = {});

    [[nodiscard]] static const void* address(const Property& property, const void* instance, const FValueLoc& loc = {});
    [[nodiscard]] static void* addressMutable(const Property& property, void* instance, const FValueLoc& loc = {});

    [[nodiscard]] static size_t containerSize(const Property& property, const void* instance);
    static bool appendEmpty(const Property& property, void* instance);
    static bool removeAt(const Property& property, void* instance, int index);
    static bool insertEmptyAt(const Property& property, void* instance, int index);
    static bool clearContainer(const Property& property, void* instance);
    static bool removeMapKey(const Property& property, void* instance, std::string_view key);
    static bool insertMapKey(const Property& property, void* instance, std::string_view key);

    [[nodiscard]] static bool equals(const Property& property, const void* a, const void* b, const FValueLoc& loc = {});
    [[nodiscard]] static bool equalsVecAxis(const Property& property,
                                            const void* a,
                                            const void* b,
                                            int axis,
                                            int componentCount,
                                            const FValueLoc& loc = {});

    [[nodiscard]] static bool tryGetVec2(const Property& property, const void* instance, glm::vec2& value, const FValueLoc& loc = {});
    static bool setVec2(const Property& property, void* instance, const glm::vec2& value, const FValueLoc& loc = {});
    [[nodiscard]] static bool tryGetVec3(const Property& property, const void* instance, glm::vec3& value, const FValueLoc& loc = {});
    static bool setVec3(const Property& property, void* instance, const glm::vec3& value, const FValueLoc& loc = {});
    [[nodiscard]] static bool tryGetVec4(const Property& property, const void* instance, glm::vec4& value, const FValueLoc& loc = {});
    static bool setVec4(const Property& property, void* instance, const glm::vec4& value, const FValueLoc& loc = {});

    [[nodiscard]] static bool tryGetFloat(const Property& property, const void* instance, float& value, const FValueLoc& loc = {});
    static bool setFloat(const Property& property, void* instance, float value, const FValueLoc& loc = {});
    [[nodiscard]] static bool tryGetInteger(const Property& property, const void* instance, int64_t& value, const FValueLoc& loc = {});
    static bool setInteger(const Property& property, void* instance, int64_t value, const FValueLoc& loc = {});
    [[nodiscard]] static bool tryGetBool(const Property& property, const void* instance, bool& value, const FValueLoc& loc = {});
    static bool setBool(const Property& property, void* instance, bool value, const FValueLoc& loc = {});
    [[nodiscard]] static bool tryGetString(const Property& property, const void* instance, std::string& value, const FValueLoc& loc = {});
    static bool setString(const Property& property, void* instance, const std::string& value, const FValueLoc& loc = {});

    [[nodiscard]] static bool tryGetEnumIndex(const Property& property, const void* instance, int& index, const FValueLoc& loc = {});
    [[nodiscard]] static bool enumLabels(const Property& property, std::vector<std::string>& labels, const FValueLoc& loc = {});
    static bool setEnumByIndex(const Property& property, void* instance, int index, const FValueLoc& loc = {});
    [[nodiscard]] static bool tryGetEnumValue(const Property& property, const void* instance, int64_t& value, const FValueLoc& loc = {});
    static bool setEnumValue(const Property& property, void* instance, int64_t value, const FValueLoc& loc = {});

    [[nodiscard]] static bool tryGetColor(const Property& property, const void* instance, glm::vec4& value, const FValueLoc& loc = {});
    static bool setColor(const Property& property, void* instance, const glm::vec4& value, const FValueLoc& loc = {});

    [[nodiscard]] static bool tryGetAssetPath(const Property& property, const void* instance, std::string& value, const FValueLoc& loc = {});
    static bool setAssetPath(const Property& property, void* instance, const std::string& value, const FValueLoc& loc = {});
    [[nodiscard]] static bool hasAssetResolveError(const Property& property, const void* instance, const FValueLoc& loc = {});

    [[nodiscard]] static bool tryGetManipulateSpec(const Property& property, Meta::ManipulateSpec& spec);
    [[nodiscard]] static std::string validationError(const Property& property, const void* instance, const FValueLoc& loc = {});

    /// Flatten serialized reflected fields, expanding nested composite types,
    /// sequence-of-leaf containers, and map-of-leaf values.
    static void collectLeaves(type_index_t rootType, const std::vector<void*>& roots, std::vector<FLeaf>& out);
};

} // namespace ya::reflection
