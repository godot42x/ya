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


/// Single-instance property access owned by the reflection layer.
/// Editor adapters (multi-select, undo, UI callbacks) sit on top of this.
struct YA_CORE_API PropertyAccessor
{
    struct FLeaf
    {
        type_index_t         ownerType = 0;
        const Property*      property  = nullptr;
        std::string          path;
        std::vector<void*>   ownerInstances;
        int                  elementIndex = -1;
    };

    [[nodiscard]] static bool isIntegerType(type_index_t typeIndex);
    [[nodiscard]] static bool isAssetRefType(type_index_t typeIndex);
    [[nodiscard]] static bool isLeafValueType(type_index_t typeIndex);
    [[nodiscard]] static bool isCompositeType(const Property& property);
    [[nodiscard]] static bool isSequenceOfLeaves(const Property& property);
    [[nodiscard]] static bool isEditable(const Property& property);
    [[nodiscard]] static bool isEnum(const Property& property, int elementIndex = -1);
    [[nodiscard]] static bool isColor(const Property& property);
    [[nodiscard]] static IContainerProperty* containerOf(const Property& property);
    [[nodiscard]] static type_index_t valueType(const Property& property, int elementIndex = -1);

    [[nodiscard]] static const void* address(const Property& property, const void* instance, int elementIndex = -1);
    [[nodiscard]] static void* addressMutable(const Property& property, void* instance, int elementIndex = -1);

    [[nodiscard]] static bool equals(const Property& property, const void* a, const void* b, int elementIndex = -1);
    [[nodiscard]] static bool equalsVecAxis(const Property& property,
                                            const void* a,
                                            const void* b,
                                            int axis,
                                            int componentCount,
                                            int elementIndex = -1);

    [[nodiscard]] static bool tryGetVec2(const Property& property, const void* instance, glm::vec2& value, int elementIndex = -1);
    static bool setVec2(const Property& property, void* instance, const glm::vec2& value, int elementIndex = -1);
    [[nodiscard]] static bool tryGetVec3(const Property& property, const void* instance, glm::vec3& value, int elementIndex = -1);
    static bool setVec3(const Property& property, void* instance, const glm::vec3& value, int elementIndex = -1);
    [[nodiscard]] static bool tryGetVec4(const Property& property, const void* instance, glm::vec4& value, int elementIndex = -1);
    static bool setVec4(const Property& property, void* instance, const glm::vec4& value, int elementIndex = -1);

    [[nodiscard]] static bool tryGetFloat(const Property& property, const void* instance, float& value, int elementIndex = -1);
    static bool setFloat(const Property& property, void* instance, float value, int elementIndex = -1);
    [[nodiscard]] static bool tryGetInteger(const Property& property, const void* instance, int64_t& value, int elementIndex = -1);
    static bool setInteger(const Property& property, void* instance, int64_t value, int elementIndex = -1);
    [[nodiscard]] static bool tryGetBool(const Property& property, const void* instance, bool& value, int elementIndex = -1);
    static bool setBool(const Property& property, void* instance, bool value, int elementIndex = -1);
    [[nodiscard]] static bool tryGetString(const Property& property, const void* instance, std::string& value, int elementIndex = -1);
    static bool setString(const Property& property, void* instance, const std::string& value, int elementIndex = -1);

    [[nodiscard]] static bool tryGetEnumIndex(const Property& property, const void* instance, int& index, int elementIndex = -1);
    [[nodiscard]] static bool enumLabels(const Property& property, std::vector<std::string>& labels, int elementIndex = -1);
    static bool setEnumByIndex(const Property& property, void* instance, int index, int elementIndex = -1);
    [[nodiscard]] static bool tryGetEnumValue(const Property& property, const void* instance, int64_t& value, int elementIndex = -1);
    static bool setEnumValue(const Property& property, void* instance, int64_t value, int elementIndex = -1);

    [[nodiscard]] static bool tryGetColor(const Property& property, const void* instance, glm::vec4& value, int elementIndex = -1);
    static bool setColor(const Property& property, void* instance, const glm::vec4& value, int elementIndex = -1);

    [[nodiscard]] static bool tryGetAssetPath(const Property& property, const void* instance, std::string& value, int elementIndex = -1);
    static bool setAssetPath(const Property& property, void* instance, const std::string& value, int elementIndex = -1);
    [[nodiscard]] static bool hasAssetResolveError(const Property& property, const void* instance, int elementIndex = -1);

    [[nodiscard]] static bool tryGetManipulateSpec(const Property& property, Meta::ManipulateSpec& spec);
    [[nodiscard]] static std::string validationError(const Property& property, const void* instance, int elementIndex = -1);

    /// Flatten serialized reflected fields, expanding nested composite types into dotted leaf paths.
    static void collectLeaves(type_index_t rootType, const std::vector<void*>& roots, std::vector<FLeaf>& out);
};

} // namespace ya::reflection
