
#pragma once

#include "ContainerProperty.h"
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
     * Serialize base classes properties to __base__ JSON object
     * @param classPtr Current class metadata
     * @param obj Object instance
     * @return JSON object containing base classes properties
     */
    static nlohmann::json serializeBaseClasses(const Class *classPtr, const void *obj);

    /**
     * Deserialize base classes properties from __base__ JSON object
     * @param classPtr Current class metadata
     * @param obj Object instance
     * @param j JSON object containing __base__ field
     */
    static void deserializeBaseClasses(const Class *classPtr, void *obj, const nlohmann::json &j);

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

    // ========================================================================
    // Unified serialization/deserialization helpers
    // ========================================================================

    /**
     * Serialize any value (scalar or complex) to JSON
     * @param valuePtr Pointer to the value
     * @param typeIndex Type index of the value
     * @return JSON representation of the value
     */
    static nlohmann::json serializeAnyValue(void *valuePtr, uint32_t typeIndex);

    /**
     * Deserialize JSON value to any type (scalar or complex)
     * @param valuePtr Pointer to the destination value
     * @param typeIndex Type index of the destination type
     * @param jsonValue JSON value to deserialize
     */
    static void deserializeAnyValue(void *valuePtr, uint32_t typeIndex, const nlohmann::json &jsonValue);

    /**
     * Convert map key to string for JSON serialization
     * @param keyPtr Pointer to the key
     * @param keyTypeIndex Type index of the key
     * @return String representation of the key
     */
    static std::string convertKeyToString(void *keyPtr, uint32_t keyTypeIndex);

    /**
     * Convert string key from JSON to actual key type
     * @param jsonKey String key from JSON
     * @param keyPtr Pointer to the destination key
     * @param keyTypeIndex Type index of the key type
     */
    static void convertStringToKey(const std::string &jsonKey, void *keyPtr, uint32_t keyTypeIndex);

    /**
     * Create, deserialize and add complex object to container
     * @param accessor Container accessor
     * @param containerPtr Pointer to the container
     * @param elementTypeIndex Type index of the element
     * @param elementJson JSON data for the element
     */
    static void deserializeComplexElement(ya::reflection::IContainerProperty *accessor, void *containerPtr,
                                          uint32_t elementTypeIndex, const nlohmann::json &elementJson);

    /**
     * Deserialize Map-like container from JSON object
     * @param accessor Container accessor
     * @param containerPtr Pointer to the container
     * @param jsonObject JSON object containing map data
     */
    static void deserializeMapContainer(ya::reflection::IContainerProperty *accessor, void *containerPtr,
                                        const nlohmann::json &jsonObject);

    /**
     * Insert basic type element into map container
     * @param accessor Container accessor
     * @param containerPtr Pointer to the container
     * @param keyPtr Pointer to the key
     * @param valueTypeIndex Type index of the value
     * @param jsonValue JSON value to deserialize
     */
    static void insertBasicMapElement(ya::reflection::IContainerProperty *accessor, void *containerPtr,
                                      void *keyPtr, uint32_t valueTypeIndex, const nlohmann::json &jsonValue);

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
            ya::type_index_v<unsigned int>,
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
