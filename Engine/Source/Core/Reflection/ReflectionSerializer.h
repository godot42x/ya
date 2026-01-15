
#pragma once

#include "Core/Common/AssetRef.h"
#include "Core/Log.h"
#include "Core/TypeIndex.h"
#include "reflects-core/lib.h"
#include <nlohmann/json.hpp>
#include <unordered_set>



namespace ya
{



struct ReflectionSerializer
{
    // MARK: Serialization


    template <typename T>
    static nlohmann::json serializeByRuntimeReflection(const T &obj, std::string className)
    {
        return serializeByRuntimeReflection(&obj, ya::type_index_v<T>, className);
    }
    template <typename T>
    static nlohmann::json serializeByRuntimeReflection(const T &obj)
    {
        return serializeByRuntimeReflection(&obj, ya::type_index_v<T>);
    }

    static nlohmann::json serializeByRuntimeReflection(const void *obj, uint32_t typeIndex, const std::string &typeName = "");
    static nlohmann::json serializeProperty(const void *obj, const Property &prop);

    // MARK: Deserialization


    static void deserializeByRuntimeReflection(void *obj, uint32_t typeIndex, const nlohmann::json &j, const std::string &className);


    template <typename T>
    static void deserializeByRuntimeReflection(T &obj, const nlohmann::json &j, const std::string &className)
    {
        auto typeIndex = ya::type_index_v<T>;
        return deserializeByRuntimeReflection(&obj, typeIndex, j, className);
    }

    static void deserializeProperty(const Property &prop, void *obj, const nlohmann::json &j);

  private:

    // MARK: helper
    // ========================================================================
    // Helper functions for scalar value serialization/deserialization
    // Using direct pointer access instead of std::any
    // ========================================================================

    /**
     * Serialize a scalar value (basic type or enum) to JSON
     * @param valuePtr Pointer to the value
     * @param prop Property metadata
     */
    static nlohmann::json serializeScalarValue(const void *valuePtr, const Property &prop);

    /**
     * Deserialize a JSON value to a scalar property
     * @param prop Property metadata
     * @param obj Object containing the property
     * @param plainValue JSON value to deserialize
     */
    static void deserializeScalarValue(const Property &prop, void *obj, const nlohmann::json &plainValue);

    /**
     * Check if a property should be serialized as a scalar value (base type or enum)
     */
    static bool is_scalar_type(const Property &prop)
    {
        return is_base_type(prop.typeIndex) || is_enum_type(prop.typeIndex);
    }

    static bool is_base_type(uint32_t typeIdx)
    {
        static std::unordered_set<uint32_t> baseTypes = {
            ya::type_index_v<int>,
            ya::type_index_v<float>,
            ya::type_index_v<double>,
            ya::type_index_v<bool>,
            ya::type_index_v<std::string>,
        };

        return baseTypes.contains(typeIdx);
    }

    /**
     * Check if a type is an enum by typeIndex
     */
    static bool is_enum_type(uint32_t typeIndex)
    {
        return EnumRegistry::instance().hasEnum(typeIndex);
    }
};

} // namespace ya
