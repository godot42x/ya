#include "ReflectionSerializer.h"
#include "PropertyExtensions.h"

namespace ya
{

// ========================================================================
// Helper: Common serialization utilities
// ========================================================================

/**
 * Serialize any value (scalar or complex) to JSON
 */
nlohmann::json ReflectionSerializer::serializeAnyValue(void *valuePtr, uint32_t typeIndex)
{
    // Handle basic types first for performance
    if (typeIndex == ya::type_index_v<int>) {
        return *static_cast<int *>(valuePtr);
    }
    if (typeIndex == ya::type_index_v<float>) {
        return *static_cast<float *>(valuePtr);
    }
    if (typeIndex == ya::type_index_v<double>) {
        return *static_cast<double *>(valuePtr);
    }
    if (typeIndex == ya::type_index_v<bool>) {
        return *static_cast<bool *>(valuePtr);
    }
    if (typeIndex == ya::type_index_v<std::string>) {
        return *static_cast<std::string *>(valuePtr);
    }
    if (typeIndex == ya::type_index_v<unsigned int>) {
        return *static_cast<unsigned int *>(valuePtr);
    }

    // Handle enums
    if (is_enum_type(typeIndex)) {
        Enum *enumInfo = EnumRegistry::instance().getEnum(typeIndex);
        if (enumInfo) {
            int64_t enumValue = 0;
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
                enumValue = *static_cast<const int *>(valuePtr);
                break;
            }
            return enumInfo->getName(enumValue);
        }
        return static_cast<int>(*static_cast<const int *>(valuePtr));
    }

    // Handle complex objects via reflection
    auto &registry = ClassRegistry::instance();
    auto *classPtr = registry.getClass(typeIndex);
    if (classPtr) {
        return serializeByRuntimeReflection(valuePtr, typeIndex, classPtr->_name);
    }

    YA_CORE_WARN("ReflectionSerializer: Unknown type for serialization (typeIndex: {})", typeIndex);
    return nullptr;
}

/**
 * Deserialize JSON value to any type (scalar or complex)
 */
void ReflectionSerializer::deserializeAnyValue(void *valuePtr, uint32_t typeIndex, const nlohmann::json &jsonValue)
{
    // Handle basic types first for performance
    if (typeIndex == ya::type_index_v<int>) {
        *static_cast<int *>(valuePtr) = jsonValue.get<int>();
        return;
    }
    if (typeIndex == ya::type_index_v<float>) {
        *static_cast<float *>(valuePtr) = jsonValue.get<float>();
        return;
    }
    if (typeIndex == ya::type_index_v<double>) {
        *static_cast<double *>(valuePtr) = jsonValue.get<double>();
        return;
    }
    if (typeIndex == ya::type_index_v<bool>) {
        *static_cast<bool *>(valuePtr) = jsonValue.get<bool>();
        return;
    }
    if (typeIndex == ya::type_index_v<std::string>) {
        *static_cast<std::string *>(valuePtr) = jsonValue.get<std::string>();
        return;
    }
    if (typeIndex == ya::type_index_v<unsigned int>) {
        *static_cast<unsigned int *>(valuePtr) = jsonValue.get<unsigned int>();
        return;
    }

    // static const std::unordered_map<type_index_t, std::function<void(void *, const nlohmann::json &)>> mapping = {
    //     {
    //         ya::type_index_v<int>,
    //         [](void *ptr, const nlohmann::json &jsonValue) {
    //             *static_cast<int *>(ptr) = jsonValue.get<int>();
    //         },
    //     },
    // };
    // if (auto it = mapping.find(typeIndex); it != mapping.end())
    // {
    //     it->second(valuePtr, jsonValue);
    //     return;
    // }



    // Handle enums
    if (is_enum_type(typeIndex)) {
        Enum *enumInfo = EnumRegistry::instance().getEnum(typeIndex);
        if (enumInfo) {
            int64_t enumValue = 0;
            if (jsonValue.is_string()) {
                try {
                    enumValue = enumInfo->getValue(jsonValue.get<std::string>());
                }
                catch (const std::exception &e) {
                    YA_CORE_WARN("ReflectionSerializer: Invalid enum name '{}': {}", jsonValue.get<std::string>(), e.what());
                    return;
                }
            }
            else if (jsonValue.is_number_integer()) {
                enumValue = jsonValue.get<int64_t>();
            }
            else {
                YA_CORE_WARN("ReflectionSerializer: Invalid JSON type for enum");
                return;
            }

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
        }
        return;
    }

    // Handle complex objects via reflection
    auto &registry = ClassRegistry::instance();
    auto *classPtr = registry.getClass(typeIndex);
    if (classPtr) {
        Property tempProp;
        tempProp.typeIndex            = typeIndex;
        tempProp.typeName             = classPtr->_name;
        tempProp.addressGetterMutable = [](void *ptr) -> void * { return ptr; };
        deserializeProperty(tempProp, valuePtr, jsonValue);
        return;
    }

    YA_CORE_WARN("ReflectionSerializer: Unknown type for deserialization (typeIndex: {})", typeIndex);
}

/**
 * Convert map key to string for JSON serialization
 */
std::string ReflectionSerializer::convertKeyToString(void *keyPtr, uint32_t keyTypeIndex)
{
    if (keyTypeIndex == ya::type_index_v<int>) {
        return std::to_string(*static_cast<int *>(keyPtr));
    }
    if (keyTypeIndex == ya::type_index_v<std::string>) {
        return *static_cast<std::string *>(keyPtr);
    }
    throw std::runtime_error("Unsupported map key type for JSON serialization");
}

/**
 * Convert string key from JSON to actual key type
 */
void ReflectionSerializer::convertStringToKey(const std::string &jsonKey, void *keyPtr, uint32_t keyTypeIndex)
{
    if (keyTypeIndex == ya::type_index_v<std::string>) {
        *static_cast<std::string *>(keyPtr) = jsonKey;
    }
    else if (keyTypeIndex == ya::type_index_v<int>) {
        try {
            *static_cast<int *>(keyPtr) = std::stoi(jsonKey);
        }
        catch (const std::exception &e) {
            throw std::runtime_error("Invalid integer key: " + jsonKey);
        }
    }
    else {
        throw std::runtime_error("Unsupported map key type for JSON deserialization");
    }
}

/**
 * Create, deserialize and add complex object to container
 */
void ReflectionSerializer::deserializeComplexElement(ya::reflection::IContainerProperty *accessor, void *containerPtr,
                                                     uint32_t elementTypeIndex, const nlohmann::json &elementJson)
{
    auto &registry     = ClassRegistry::instance();
    auto *elementClass = registry.getClass(elementTypeIndex);
    if (!elementClass) {
        YA_CORE_WARN("ReflectionSerializer: Container element type '{}' not found in registry", elementTypeIndex);
        return;
    }

    if (!elementClass->canCreateInstance()) {
        YA_CORE_WARN("ReflectionSerializer: Cannot create instance of type '{}'", elementClass->_name);
        return;
    }

    void *elementPtr = nullptr;
    try {
        elementPtr = elementClass->createInstance();

        Property elementProp;
        elementProp.typeIndex            = elementTypeIndex;
        elementProp.typeName             = elementClass->_name;
        elementProp.addressGetter        = nullptr;
        elementProp.addressGetterMutable = [](void *ptr) -> void * { return ptr; };

        deserializeProperty(elementProp, elementPtr, elementJson);
        accessor->addElement(containerPtr, elementPtr);
        elementClass->destroyInstance(elementPtr);
    }
    catch (const std::exception &e) {
        YA_CORE_WARN("ReflectionSerializer: Failed to deserialize complex element: {}", e.what());
        if (elementPtr && elementClass->canCreateInstance()) {
            elementClass->destroyInstance(elementPtr);
        }
    }
}

/**
 * Deserialize Map-like container from JSON object
 */
void ReflectionSerializer::deserializeMapContainer(ya::reflection::IContainerProperty *accessor, void *containerPtr,
                                                   const nlohmann::json &jsonObject)
{
    if (!jsonObject.is_object()) {
        YA_CORE_WARN("ReflectionSerializer: Expected JSON object for map container");
        return;
    }

    uint32_t keyTypeIndex   = accessor->getKeyTypeIndex();
    uint32_t valueTypeIndex = accessor->getElementTypeIndex();

    for (auto it = jsonObject.begin(); it != jsonObject.end(); ++it) {
        const std::string &jsonKey   = it.key();
        const auto        &jsonValue = it.value();

        try {
            // Handle different key-value type combinations
            if (is_base_type(valueTypeIndex)) {
                // For basic value types, create temporary storage
                if (keyTypeIndex == ya::type_index_v<std::string>) {
                    std::string key = jsonKey;
                    insertBasicMapElement(accessor, containerPtr, &key, valueTypeIndex, jsonValue);
                }
                else if (keyTypeIndex == ya::type_index_v<int>) {
                    int key = std::stoi(jsonKey);
                    insertBasicMapElement(accessor, containerPtr, &key, valueTypeIndex, jsonValue);
                }
                else {
                    YA_CORE_WARN("ReflectionSerializer: Unsupported map key type (typeIndex: {})", keyTypeIndex);
                }
            }
            else {
                // For complex value types
                auto &registry   = ClassRegistry::instance();
                auto *valueClass = registry.getClass(valueTypeIndex);
                if (valueClass && valueClass->canCreateInstance()) {
                    void *valuePtr = valueClass->createInstance();
                    try {
                        deserializeAnyValue(valuePtr, valueTypeIndex, jsonValue);

                        if (keyTypeIndex == ya::type_index_v<std::string>) {
                            std::string key = jsonKey;
                            accessor->insertElement(containerPtr, &key, valuePtr);
                        }
                        else if (keyTypeIndex == ya::type_index_v<int>) {
                            int key = std::stoi(jsonKey);
                            accessor->insertElement(containerPtr, &key, valuePtr);
                        }

                        valueClass->destroyInstance(valuePtr);
                    }
                    catch (const std::exception &e) {
                        valueClass->destroyInstance(valuePtr);
                        throw;
                    }
                }
            }
        }
        catch (const std::exception &e) {
            YA_CORE_WARN("ReflectionSerializer: Failed to deserialize map entry '{}': {}", jsonKey, e.what());
        }
    }
}

/**
 * Insert basic type element into map container
 */
void ReflectionSerializer::insertBasicMapElement(ya::reflection::IContainerProperty *accessor, void *containerPtr,
                                                 void *keyPtr, uint32_t valueTypeIndex, const nlohmann::json &jsonValue)
{
    if (valueTypeIndex == ya::type_index_v<int>) {
        int value = jsonValue.get<int>();
        accessor->insertElement(containerPtr, keyPtr, &value);
    }
    else if (valueTypeIndex == ya::type_index_v<float>) {
        float value = jsonValue.get<float>();
        accessor->insertElement(containerPtr, keyPtr, &value);
    }
    else if (valueTypeIndex == ya::type_index_v<double>) {
        double value = jsonValue.get<double>();
        accessor->insertElement(containerPtr, keyPtr, &value);
    }
    else if (valueTypeIndex == ya::type_index_v<bool>) {
        bool value = jsonValue.get<bool>();
        accessor->insertElement(containerPtr, keyPtr, &value);
    }
    else if (valueTypeIndex == ya::type_index_v<std::string>) {
        std::string value = jsonValue.get<std::string>();
        accessor->insertElement(containerPtr, keyPtr, &value);
    }
    else {
        YA_CORE_WARN("ReflectionSerializer: Unsupported basic value type (typeIndex: {})", valueTypeIndex);
    }
}

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

    // ★ NEW: Handle pointer types - dereference and serialize pointee
    if (prop.bPointer && prop.pointeeTypeIndex != 0) {
        void * const *ptrLocation = static_cast<void * const *>(valuePtr);
        void *pointee = ptrLocation ? *ptrLocation : nullptr;
        
        if (pointee) {
            // Serialize the pointee object
            j = serializeAnyValue(pointee, prop.pointeeTypeIndex);
        } else {
            // Null pointer - serialize as null
            j = nullptr;
        }
        return j;
    }

    if (is_scalar_type(prop))
    {
        j = serializeScalarValue(valuePtr, prop);
        return j;
    }

    // ★ NEW: Check if it's a container type
    if (::ya::reflection::PropertyContainerHelper::isContainer(prop)) {
        auto *accessor = ::ya::reflection::PropertyContainerHelper::getContainerAccessor(prop);
        if (accessor && accessor->isMapLike()) {
            // Serialize Map-like containers - 初始化为空对象
            j = nlohmann::json::object();
            ::ya::reflection::PropertyContainerHelper::iterateMapContainer(
                (prop),
                const_cast<void *>(valuePtr),
                [&j](void *keyPtr, uint32_t keyTypeIndex, void *valuePtr, uint32_t valueTypeIndex) {
                    // Convert key to string and serialize value using unified helpers
                    std::string keyStr = convertKeyToString(keyPtr, keyTypeIndex);
                    j[keyStr]          = serializeAnyValue(valuePtr, valueTypeIndex);
                });
        }
        else {
            // Serialize Vector/Set-like containers
            j = nlohmann::json::array();
            ::ya::reflection::PropertyContainerHelper::iterateContainer(
                const_cast<Property &>(prop),
                const_cast<void *>(valuePtr),
                [&j](size_t index, void *elementPtr, uint32_t elementTypeIndex) {
                    // Serialize container element using unified helper
                    j.push_back(serializeAnyValue(elementPtr, elementTypeIndex));
                });
        }
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
    // ★ NEW: Handle pointer types - allocate and deserialize pointee
    if (prop.bPointer && prop.pointeeTypeIndex != 0) {
        void **ptrLocation = static_cast<void **>(prop.getMutableAddress(obj));
        if (!ptrLocation) {
            YA_CORE_WARN("ReflectionSerializer: Cannot get mutable address for pointer property '{}'", prop.name);
            return;
        }

        // If JSON is null, set pointer to nullptr
        if (j.is_null()) {
            // Clean up existing pointee if any
            if (*ptrLocation) {
                auto &registry = ClassRegistry::instance();
                auto *classPtr = registry.getClass(prop.pointeeTypeIndex);
                if (classPtr && classPtr->canCreateInstance()) {
                    classPtr->destroyInstance(*ptrLocation);
                }
            }
            *ptrLocation = nullptr;
            return;
        }

        // Allocate new instance for pointee
        auto &registry = ClassRegistry::instance();
        auto *classPtr = registry.getClass(prop.pointeeTypeIndex);
        if (!classPtr) {
            YA_CORE_WARN("ReflectionSerializer: Pointee class not found for pointer property '{}' (typeIndex: {})", 
                         prop.name, prop.pointeeTypeIndex);
            return;
        }

        if (!classPtr->canCreateInstance()) {
            YA_CORE_WARN("ReflectionSerializer: Cannot create instance for pointee type '{}'", classPtr->_name);
            return;
        }

        // Clean up old pointee if exists
        if (*ptrLocation) {
            classPtr->destroyInstance(*ptrLocation);
        }

        // Create new pointee instance
        void *pointee = classPtr->createInstance();
        if (!pointee) {
            YA_CORE_WARN("ReflectionSerializer: Failed to create pointee instance for '{}'", prop.name);
            return;
        }

        // Deserialize into the pointee
        try {
            deserializeAnyValue(pointee, prop.pointeeTypeIndex, j);
            *ptrLocation = pointee;
        } catch (const std::exception &e) {
            YA_CORE_WARN("ReflectionSerializer: Failed to deserialize pointer property '{}': {}", prop.name, e.what());
            classPtr->destroyInstance(pointee);
            *ptrLocation = nullptr;
        }
        return;
    }

    // ★ NEW: Check if it's a container type
    if (::ya::reflection::PropertyContainerHelper::isContainer(prop)) {
        auto *accessor = ::ya::reflection::PropertyContainerHelper::getContainerAccessor(prop);
        if (!accessor) {
            YA_CORE_WARN("ReflectionSerializer: Container accessor not found for property '{}'", prop.name);
            return;
        }

        void *containerPtr = prop.getMutableAddress(obj);
        if (!containerPtr) {
            YA_CORE_WARN("ReflectionSerializer: Cannot get mutable address for container '{}'", prop.name);
            return;
        }

        // Clear existing content before deserializing
        accessor->clear(containerPtr);

        if (accessor->isMapLike()) {
            // Deserialize Map-like containers (std::map, std::unordered_map)
            deserializeMapContainer(accessor, containerPtr, j);
        }
        else {
            // Deserialize Vector/Set-like containers (std::vector, std::set, std::unordered_set)
            if (j.is_array()) {
                uint32_t elementTypeIndex = accessor->getElementTypeIndex();

                for (size_t i = 0; i < j.size(); ++i) {
                    const auto &elementJson = j[i];

                    // Create a temporary Property for the element
                    Property elementProp;
                    elementProp.typeIndex            = elementTypeIndex;
                    elementProp.addressGetterMutable = [](void *ptr) -> void * { return ptr; };

                    // Deserialize container element using unified helpers
                    if (is_scalar_type(elementProp)) {
                        // For scalar types, handle each type specifically to avoid storage issues
                        if (elementTypeIndex == ya::type_index_v<int>) {
                            int value = elementJson.get<int>();
                            accessor->addElement(containerPtr, &value);
                        }
                        else if (elementTypeIndex == ya::type_index_v<float>) {
                            float value = elementJson.get<float>();
                            accessor->addElement(containerPtr, &value);
                        }
                        else if (elementTypeIndex == ya::type_index_v<double>) {
                            double value = elementJson.get<double>();
                            accessor->addElement(containerPtr, &value);
                        }
                        else if (elementTypeIndex == ya::type_index_v<bool>) {
                            bool value = elementJson.get<bool>();
                            accessor->addElement(containerPtr, &value);
                        }
                        else if (elementTypeIndex == ya::type_index_v<std::string>) {
                            std::string value = elementJson.get<std::string>();
                            accessor->addElement(containerPtr, &value);
                        }
                    }
                    else {
                        // For complex types, use the unified complex element deserializer
                        deserializeComplexElement(accessor, containerPtr, elementTypeIndex, elementJson);
                    }
                }
            }
        }
        return;
    }

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

        // 递归查找属性（包括父类），兼容没有 __base__ 的 JSON 格式
        auto *subProp = classPtr->findPropertyRecursive(jsonKey);
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
    // Delegate to the more general serializeAnyValue function
    return serializeAnyValue(const_cast<void *>(valuePtr), prop.typeIndex);
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

    // Delegate to the more general deserializeAnyValue function
    deserializeAnyValue(valuePtr, prop.typeIndex, plainValue);
}

} // namespace ya
