#include "ReflectionSerializer.h"

namespace ya
{

// ========================================================================
// Helper: Serialize base classes
// ========================================================================
nlohmann::json ReflectionSerializer::serializeBaseClasses(const Class *classPtr, const void *obj)
{
    nlohmann::json baseJson;

    for (auto parentTypeId : classPtr->parents) {
        Class *parentClass = classPtr->getClassByTypeId(parentTypeId);
        if (parentClass) {
            const void *parentObj = classPtr->getParentPointer(const_cast<void *>(obj), parentTypeId);
            if (parentObj) {
                nlohmann::json parentJson;

                // 1. 递归序列化父类的父类（保持层级结构）
                nlohmann::json parentBaseJson = serializeBaseClasses(parentClass, parentObj);
                if (!parentBaseJson.empty()) {
                    parentJson["__base__"] = parentBaseJson;
                }

                // 2. 序列化父类自己的属性（不递归，只访问当前类）
                parentClass->visitAllProperties(
                    parentObj,
                    [&parentJson](const std::string &propName, const Property &prop, const void *propObj) {
                        if (prop.metadata.hasFlag(FieldFlags::NotSerialized)) {
                            return;
                        }
                        try {
                            parentJson[propName] = serializeProperty(propObj, prop);
                        }
                        catch (const std::exception &e) {
                            YA_CORE_WARN("ReflectionSerializer: Failed to serialize base property '{}': {}", propName, e.what());
                        }
                    },
                    false // recursive = false，只访问当前类属性
                );

                if (!parentJson.empty()) {
                    baseJson[parentClass->_name] = parentJson;
                }
            }
        }
    }

    return baseJson;
}

// ========================================================================
// Helper: Deserialize base classes
// ========================================================================
void ReflectionSerializer::deserializeBaseClasses(const Class *classPtr, void *obj, const nlohmann::json &j)
{
    if (!j.contains("__base__") || !j["__base__"].is_object()) {
        return;
    }

    const auto &baseJson = j["__base__"];

    for (auto parentTypeId : classPtr->parents) {
        Class *parentClass = classPtr->getClassByTypeId(parentTypeId);
        if (parentClass) {
            void *parentObj = classPtr->getParentPointer(obj, parentTypeId);
            if (parentObj) {
                if (baseJson.contains(parentClass->_name) && baseJson[parentClass->_name].is_object()) {
                    const auto &parentJson = baseJson[parentClass->_name];

                    for (auto it = parentJson.begin(); it != parentJson.end(); ++it) {
                        const std::string &jsonKey   = it.key();
                        const auto        &jsonValue = it.value();

                        auto *prop = parentClass->findPropertyRecursive(jsonKey);
                        if (!prop) {
                            YA_CORE_WARN("ReflectionSerializer: Base property '{}.{}' not found", parentClass->_name, jsonKey);
                            continue;
                        }

                        try {
                            if (is_scalar_type(*prop)) {
                                deserializeScalarValue(*prop, parentObj, jsonValue);
                            }
                            else {
                                deserializeProperty(*prop, parentObj, jsonValue);
                            }
                        }
                        catch (const std::exception &e) {
                            YA_CORE_WARN("ReflectionSerializer: Failed to deserialize base property '{}.{}': {}",
                                         parentClass->_name,
                                         jsonKey,
                                         e.what());
                        }
                    }
                }
            }
        }
    }
}

nlohmann::json ReflectionSerializer::serializeByRuntimeReflection(const void *obj, uint32_t typeIndex, const std::string &typeName)
{
    auto &registry = ClassRegistry::instance();
    auto *classPtr = registry.getClass(typeIndex);

    if (!classPtr) {
        YA_CORE_WARN("ReflectionSerializer: Class '{}:{}' not found in registry", typeIndex, typeName);
        classPtr = registry.getClass(typeName);
        if (!classPtr) {
            return {};
        }
    }

    nlohmann::json j;

    // 1. 序列化父类属性到 __base__ 对象
    nlohmann::json baseJson = serializeBaseClasses(classPtr, obj);

    // 2. 遍历当前类的属性（不包括父类）
    for (const auto &[propName, prop] : classPtr->properties) {
        if (prop.metadata.hasFlag(FieldFlags::NotSerialized)) {
            continue;
        }

        try {
            j[propName] = ReflectionSerializer::serializeProperty(obj, prop);
        }
        catch (const std::exception &e) {
            YA_CORE_WARN("ReflectionSerializer: Failed to serialize property '{}.{}': {}",
                         classPtr->_name,
                         propName,
                         e.what());
        }
    }

    // 3. 如果有父类属性，添加 __base__ 字段
    if (!baseJson.empty()) {
        j["__base__"] = baseJson;
    }

    return j;
}

/**
 * Serialize property using runtime reflection registry
 */
nlohmann::json ReflectionSerializer::serializeProperty(const void *obj, const Property &prop)
{
    nlohmann::json j;

    // Get pointer to the property value
    const void *valuePtr = prop.getAddress(obj);
    if (!valuePtr) {
        YA_CORE_WARN("ReflectionSerializer: Cannot get address for property '{}'", prop.name);
        return j;
    }

    if (is_scalar_type(prop))
    {
        j = serializeScalarValue(valuePtr, prop);
        return j;
    }

    auto &registry = ClassRegistry::instance();
    auto *classPtr = registry.getClass(prop.typeIndex);

    if (!classPtr) {
        YA_CORE_WARN("ReflectionSerializer: Class '{}' not found in registry", prop.getTypeName());
        classPtr = registry.getClass(prop.getTypeName());
        return j;
    }

    // For nested objects, valuePtr is already the pointer to the nested object
    const void *nestedObjPtr = valuePtr;

    // 1. 序列化父类属性到 __base__ 对象
    nlohmann::json baseJson = serializeBaseClasses(classPtr, nestedObjPtr);

    // 2. 遍历当前类的属性（不包括父类）
    for (const auto &[propName, subProp] : classPtr->properties) {
        if (subProp.metadata.hasFlag(FieldFlags::NotSerialized)) {
            continue;
        }

        try {
            if (is_scalar_type(subProp))
            {
                const void *subValuePtr = subProp.getAddress(nestedObjPtr);
                if (subValuePtr) {
                    j[propName] = serializeScalarValue(subValuePtr, subProp);
                }
            }
            else {
                // Nested object: recursive serialization
                j[propName] = serializeProperty(nestedObjPtr, subProp);
            }
        }
        catch (const std::exception &e) {
            YA_CORE_WARN("ReflectionSerializer: Failed to serialize nested property '{}.{}': {}",
                         classPtr->_name,
                         propName,
                         e.what());
        }
    }

    // 3. 如果有父类属性，添加 __base__ 字段
    if (!baseJson.empty()) {
        j["__base__"] = baseJson;
    }

    return j;
}

void ReflectionSerializer::deserializeByRuntimeReflection(void *obj, uint32_t typeIndex, const nlohmann::json &j, const std::string &className)
{

    auto &registry = ClassRegistry::instance();
    auto *classPtr = registry.getClass(typeIndex);

    if (!classPtr) {
        YA_CORE_WARN("ReflectionSerializer: Class '{}' not found in registry", className);
        return;
    }

    // 1. 先反序列化父类属性（从 __base__ 对象）
    deserializeBaseClasses(classPtr, obj, j);

    // 2. 反序列化当前类的属性
    for (auto it = j.begin(); it != j.end(); ++it) {
        const std::string &jsonKey = it.key();

        // 跳过 __base__ 字段（已处理）
        if (jsonKey == "__base__") {
            continue;
        }

        const auto &jsonValue = it.value();

        // 只查找当前类的属性（不递归查找父类）
        auto *prop = classPtr->getProperty(jsonKey);
        if (!prop) {
            YA_CORE_WARN("ReflectionSerializer: Property '{}.{}' not found", className, jsonKey);
            continue;
        }

        try {
            deserializeProperty(*prop, obj, jsonValue);
        }
        catch (const std::exception &e) {
            YA_CORE_WARN("ReflectionSerializer: Failed to deserialize property '{}.{}': {}",
                         className,
                         jsonKey,
                         e.what());
        }
    }
}
void ReflectionSerializer::deserializeProperty(const Property &prop, void *obj, const nlohmann::json &j)
{
    if (is_scalar_type(prop))
    {
        deserializeScalarValue(prop, obj, j);
        return;
    }

    auto &registry = ClassRegistry::instance();
    auto *classPtr = registry.getClass(prop.typeIndex);
    if (!classPtr) {
        YA_CORE_WARN("ReflectionSerializer: Class '{}' not found in registry", prop.getTypeName());
        return;
    }

    // Get mutable address for nested object
    void *nestedObjPtr = prop.getMutableAddress(obj);
    if (!nestedObjPtr) {
        YA_CORE_WARN("ReflectionSerializer: Cannot get mutable address for nested object '{}'", prop.getTypeName());
        return;
    }

    // 1. 先反序列化父类属性（从 __base__ 对象）
    deserializeBaseClasses(classPtr, nestedObjPtr, j);

    // 2. 反序列化当前类的属性
    for (auto it = j.begin(); it != j.end(); ++it) {
        const std::string &jsonKey = it.key();

        // 跳过 __base__ 字段（已处理）
        if (jsonKey == "__base__") {
            continue;
        }

        const auto &jsonValue = it.value();

        // 只查找当前类的属性（不递归查找父类）
        auto *subProp = classPtr->getProperty(jsonKey);
        if (!subProp) {
            YA_CORE_WARN("ReflectionSerializer: Property '{}.{}' not found", classPtr->_name, jsonKey);
            continue;
        }

        try {
            if (is_scalar_type(*subProp))
            {
                deserializeScalarValue(*subProp, nestedObjPtr, jsonValue);
            }
            else {
                // Recursive deserialization of nested object
                deserializeProperty(*subProp, nestedObjPtr, jsonValue);
            }
        }
        catch (const std::exception &e) {
            YA_CORE_WARN("ReflectionSerializer: Failed to deserialize property '{}.{}': {}",
                         classPtr->_name,
                         jsonKey,
                         e.what());
        }
    }

    // TODO: async in another thread?
    // ★ NEW: After deserialization, resolve asset references if applicable
    auto &resolver = DefaultAssetRefResolver::instance();
    if (resolver.isAssetRefType(prop.typeIndex)) {
        resolver.resolveAssetRef(prop.typeIndex, nestedObjPtr);
    }
}

/**
 * Serialize scalar value (basic types or enums) to JSON using direct pointer access
 */
nlohmann::json ReflectionSerializer::serializeScalarValue(const void *valuePtr, const Property &prop)
{
    auto typeIdx = prop.typeIndex;

    // Basic types - direct pointer dereference
    if (typeIdx == ya::TypeIndex<int>::value()) {
        return *static_cast<const int *>(valuePtr);
    }
    if (typeIdx == ya::TypeIndex<float>::value()) {
        return *static_cast<const float *>(valuePtr);
    }
    if (typeIdx == ya::TypeIndex<double>::value()) {
        return *static_cast<const double *>(valuePtr);
    }
    if (typeIdx == ya::TypeIndex<bool>::value()) {
        return *static_cast<const bool *>(valuePtr);
    }
    if (typeIdx == ya::TypeIndex<std::string>::value()) {
        return *static_cast<const std::string *>(valuePtr);
    }

    // Check if it's an enum type by typeIndex
    Enum *enumInfo = EnumRegistry::instance().getEnum(typeIdx);
    if (enumInfo) {
        // Get enum value based on underlying type size
        // We read the raw memory and interpret it as int64_t
        int64_t enumValue = 0;

        // Most enums use int as underlying type, but enum class can use uint8_t etc.
        // We'll read based on common sizes
        switch (enumInfo->underlyingSize) {
        case 1:
            enumValue = *static_cast<const uint8_t *>(valuePtr);
            break;
        case 2:
            enumValue = *static_cast<const uint16_t *>(valuePtr);
            break;
        case 4:
            enumValue = *static_cast<const int32_t *>(valuePtr);
            break;
        case 8:
            enumValue = *static_cast<const int64_t *>(valuePtr);
            break;
        default:
            // Fallback to int
            enumValue = *static_cast<const int *>(valuePtr);
            break;
        }

        return enumInfo->getName(enumValue);
    }

    YA_CORE_WARN("ReflectionSerializer: Unsupported type for property (typeIndex: {})", typeIdx);
    return {};
}

/**
 * Deserialize JSON value to object property using direct pointer access
 */
void ReflectionSerializer::deserializeScalarValue(const Property &prop, void *obj, const nlohmann::json &plainValue)
{
    void *valuePtr = prop.getMutableAddress(obj);
    if (!valuePtr) {
        YA_CORE_WARN("ReflectionSerializer: Cannot get mutable address for property '{}'", prop.name);
        return;
    }

    auto        typeIdx        = prop.typeIndex;
    static auto intTypeIndx    = ya::type_index_v<int>;
    static auto floatTypeIndx  = ya::type_index_v<float>;
    static auto doubleTypeIndx = ya::type_index_v<double>;
    static auto boolTypeIndx   = ya::type_index_v<bool>;
    static auto stringTypeIndx = ya::type_index_v<std::string>;

    // Basic types - direct pointer write
    if (typeIdx == intTypeIndx) {
        *static_cast<int *>(valuePtr) = plainValue.get<int>();
        return;
    }
    if (typeIdx == floatTypeIndx) {
        *static_cast<float *>(valuePtr) = plainValue.get<float>();
        return;
    }
    if (typeIdx == doubleTypeIndx) {
        *static_cast<double *>(valuePtr) = plainValue.get<double>();
        return;
    }
    if (typeIdx == boolTypeIndx) {
        *static_cast<bool *>(valuePtr) = plainValue.get<bool>();
        return;
    }
    if (typeIdx == stringTypeIndx) {
        *static_cast<std::string *>(valuePtr) = plainValue.get<std::string>();
        return;
    }

    // Check if it's an enum type by typeIndex
    Enum *enumInfo = EnumRegistry::instance().getEnum(typeIdx);
    if (enumInfo) {
        int64_t enumValue = 0;

        if (plainValue.is_string()) {
            // Deserialize from enum name string
            try {
                enumValue = enumInfo->getValue(plainValue.get<std::string>());
            }
            catch (const std::exception &e) {
                YA_CORE_WARN("ReflectionSerializer: Invalid enum name '{}' for enum '{}': {}",
                             plainValue.get<std::string>(),
                             enumInfo->name,
                             e.what());
                return;
            }
        }
        else if (plainValue.is_number_integer()) {
            // Deserialize from integer
            enumValue = plainValue.get<int64_t>();
        }
        else {
            YA_CORE_WARN("ReflectionSerializer: Invalid JSON type for enum '{}'", enumInfo->name);
            return;
        }

        // Write enum value based on underlying type size
        switch (enumInfo->underlyingSize) {
        case 1:
            *static_cast<uint8_t *>(valuePtr) = static_cast<uint8_t>(enumValue);
            break;
        case 2:
            *static_cast<uint16_t *>(valuePtr) = static_cast<uint16_t>(enumValue);
            break;
        case 4:
            *static_cast<int32_t *>(valuePtr) = static_cast<int32_t>(enumValue);
            break;
        case 8:
            *static_cast<int64_t *>(valuePtr) = enumValue;
            break;
        default:
            *static_cast<int *>(valuePtr) = static_cast<int>(enumValue);
            break;
        }
        return;
    }

    YA_CORE_WARN("ReflectionSerializer: Unsupported type for property (typeIndex: {})", typeIdx);
}

} // namespace ya
