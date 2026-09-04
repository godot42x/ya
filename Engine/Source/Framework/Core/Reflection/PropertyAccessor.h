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
    };

    [[nodiscard]] static bool isIntegerType(type_index_t typeIndex);
    [[nodiscard]] static bool isAssetRefType(type_index_t typeIndex);
    [[nodiscard]] static bool isLeafValueType(type_index_t typeIndex);
    [[nodiscard]] static bool isCompositeType(const Property& property);
    [[nodiscard]] static bool isEditable(const Property& property);
    [[nodiscard]] static bool isEnum(const Property& property);
    [[nodiscard]] static bool isColor(const Property& property);

    [[nodiscard]] static const void* address(const Property& property, const void* instance);
    [[nodiscard]] static void* addressMutable(const Property& property, void* instance);

    [[nodiscard]] static bool equals(const Property& property, const void* a, const void* b);
    [[nodiscard]] static bool equalsVecAxis(const Property& property,
                                            const void* a,
                                            const void* b,
                                            int axis,
                                            int componentCount);

    [[nodiscard]] static bool tryGetVec2(const Property& property, const void* instance, glm::vec2& value);
    static bool setVec2(const Property& property, void* instance, const glm::vec2& value);
    [[nodiscard]] static bool tryGetVec3(const Property& property, const void* instance, glm::vec3& value);
    static bool setVec3(const Property& property, void* instance, const glm::vec3& value);
    [[nodiscard]] static bool tryGetVec4(const Property& property, const void* instance, glm::vec4& value);
    static bool setVec4(const Property& property, void* instance, const glm::vec4& value);

    [[nodiscard]] static bool tryGetFloat(const Property& property, const void* instance, float& value);
    static bool setFloat(const Property& property, void* instance, float value);
    [[nodiscard]] static bool tryGetInteger(const Property& property, const void* instance, int64_t& value);
    static bool setInteger(const Property& property, void* instance, int64_t value);
    [[nodiscard]] static bool tryGetBool(const Property& property, const void* instance, bool& value);
    static bool setBool(const Property& property, void* instance, bool value);
    [[nodiscard]] static bool tryGetString(const Property& property, const void* instance, std::string& value);
    static bool setString(const Property& property, void* instance, const std::string& value);

    [[nodiscard]] static bool tryGetEnumIndex(const Property& property, const void* instance, int& index);
    [[nodiscard]] static bool enumLabels(const Property& property, std::vector<std::string>& labels);
    static bool setEnumByIndex(const Property& property, void* instance, int index);
    [[nodiscard]] static bool tryGetEnumValue(const Property& property, const void* instance, int64_t& value);
    static bool setEnumValue(const Property& property, void* instance, int64_t value);

    [[nodiscard]] static bool tryGetColor(const Property& property, const void* instance, glm::vec4& value);
    static bool setColor(const Property& property, void* instance, const glm::vec4& value);

    [[nodiscard]] static bool tryGetAssetPath(const Property& property, const void* instance, std::string& value);
    static bool setAssetPath(const Property& property, void* instance, const std::string& value);
    [[nodiscard]] static bool hasAssetResolveError(const Property& property, const void* instance);

    [[nodiscard]] static bool tryGetManipulateSpec(const Property& property, Meta::ManipulateSpec& spec);
    [[nodiscard]] static std::string validationError(const Property& property, const void* instance);

    /// Flatten serialized reflected fields, expanding nested composite types into dotted leaf paths.
    static void collectLeaves(type_index_t rootType, const std::vector<void*>& roots, std::vector<FLeaf>& out);
};

} // namespace ya::reflection
