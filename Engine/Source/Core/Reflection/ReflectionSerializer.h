
#pragma once

#include "Core/Log.h"
#include <nlohmann/json.hpp>
#include <reflects-core/lib.h>
#include <unordered_set>

namespace ya::reflection::detail
{
// 前向声明 ExternalReflect
template <typename T>
struct ExternalReflect;
} // namespace ya::reflection::detail

namespace ya
{

namespace serializer_detail
{
// 检测是否支持外部反射
template <typename T, typename = void>
struct has_external_reflect : std::false_type
{};

template <typename T>
struct has_external_reflect<T, std::void_t<
                                   decltype(::ya::reflection::detail::ExternalReflect<T>::visit_properties(
                                       std::declval<T &>(),
                                       std::declval<int (*)(const char *, int &)>()))>> : std::true_type
{};

template <typename T>
inline constexpr bool has_external_reflect_v = has_external_reflect<T>::value;
} // namespace serializer_detail


struct ReflectionSerializer
{
    // MARK: Serialization


    template <typename T>
    static nlohmann::json serializeByRuntimeReflection(const T &obj, std::string className)
    {
        return serializeByRuntimeReflection(obj, ya::type_index_v<T>, className);
    }
    template <typename T>
    static nlohmann::json serializeByRuntimeReflection(const T &obj)
    {
        return serializeByRuntimeReflection(obj, ya::type_index_v<T>);
    }

    template <typename T>
    static nlohmann::json serializeByRuntimeReflection(const T &obj, uint32_t typeIndex, const std::string &typeName = "")
    {
        nlohmann::json j;

        auto &registry = ClassRegistry::instance();
        auto *classPtr = registry.getClass(typeIndex);

        if (!classPtr) {
            YA_CORE_WARN("ReflectionSerializer: Class '{}:{}' not found in registry", typeIndex, typeName);
            classPtr = registry.getClass(typeName);
        }

        for (const auto &[propName, prop] : classPtr->properties) {

            // if (prop.metadata.hasFlag(FieldFlags::NotSerialized)) {
            //     continue;
            // }

            try {
                j[propName] = ReflectionSerializer::serializeProperty(&obj, prop);
            }
            catch (const std::exception &e) {
                YA_CORE_WARN("ReflectionSerializer: Failed to serialize property '{}.{}': {}",
                             classPtr->_name,
                             propName,
                             e.what());
            }
        }

        return j;
    }
    /**
     * 通过运行时反射 registry 序列化对象
     */
    static nlohmann::json serializeProperty(const void *obj, const Property &prop)
    {
        nlohmann::json j;

        if (is_base_type(prop.typeIndex))
        {
            // getter 不修改对象，但签名为 void*，这里安全去除 const
            j = serializeBasicAnyValue(prop.getter(const_cast<void *>(obj)), prop);
            return j;
        }

        auto &registry = ClassRegistry::instance();
        auto *classPtr = registry.getClass(prop.typeIndex);

        if (!classPtr) {
            YA_CORE_WARN("ReflectionSerializer: Class '{}' not found in registry", prop.getTypeName());
            classPtr = registry.getClass(prop.getTypeName());
            return j;
        }

        // 对嵌套对象，需要先获取其地址
        const void *nestedObjPtr = prop.addressGetter ? prop.addressGetter(obj) : obj;
        if (!nestedObjPtr) {
            YA_CORE_WARN("ReflectionSerializer: Cannot get address for nested object '{}'", prop.getTypeName());
            return j;
        }

        // 遍历嵌套对象的所有属性
        for (const auto &[propName, subProp] : classPtr->properties) {
            // 跳过标记为 NotSerialized 的属性
            if (subProp.metadata.hasFlag(FieldFlags::NotSerialized)) {
                continue;
            }

            try {
                if (is_base_type(subProp.typeIndex))
                {
                    // 基础类型：通过 getter 获取值（getter 不修改对象，但签名为 void*）
                    std::any value = subProp.getter(const_cast<void *>(nestedObjPtr));
                    j[propName]    = serializeBasicAnyValue(value, subProp);
                }
                else {
                    // 嵌套对象：递归序列化
                    j[propName] = serializeProperty(nestedObjPtr, subProp);
                }
            }
            catch (const std::exception &e) {
                YA_CORE_WARN("ReflectionSerializer: Failed to serialize property '{}.{}': {}",
                             classPtr->_name,
                             propName,
                             e.what());
            }
        }

        return j;
    }

    // MARK: Deserialization


    /**
     * 通过运行时反射 registry 就地反序列化对象
     */
    template <typename T>
    static void deserializeByRuntimeReflection(T &obj, const nlohmann::json &j, const std::string &className)
    {

        auto &registry  = ClassRegistry::instance();
        auto  typeIndex = ya::type_index_v<T>;
        auto *classPtr  = registry.getClass(typeIndex);

        if (!classPtr) {
            YA_CORE_WARN("ReflectionSerializer: Class '{}' not found in registry", className);
            return;
        }

        // 遍历 JSON 中的所有字段
        for (auto it = j.begin(); it != j.end(); ++it) {
            const std::string &jsonKey   = it.key();
            const auto        &jsonValue = it.value();

            auto *prop = classPtr->getProperty(jsonKey);
            if (!prop) {
                YA_CORE_WARN("ReflectionSerializer: Property '{}.{}' not found", className, jsonKey);
                continue;
            }

            // if (prop->metadata.hasFlag(FieldFlags::NotSerialized)) {
            //     continue;
            // }

            try {
                deserializeProperty(*prop, &obj, jsonValue);
            }
            catch (const std::exception &e) {
                YA_CORE_WARN("ReflectionSerializer: Failed to deserialize property '{}.{}': {}",
                             className,
                             jsonKey,
                             e.what());
            }
        }
    }

    static void deserializeProperty(const Property &prop, void *obj, const nlohmann::json &j)
    {
        if (is_base_type(prop.typeIndex))
        {
            deserializeAnyValue(prop, obj, j);
            return;
        }

        auto &registry = ClassRegistry::instance();
        auto *classPtr = registry.getClass(prop.typeIndex);
        if (!classPtr) {
            YA_CORE_WARN("ReflectionSerializer: Class '{}' not found in registry", prop.getTypeName());
            return;
        }

        // 对嵌套对象，需要先获取其可写地址
        void *nestedObjPtr = prop.addressGetterMutable ? prop.addressGetterMutable(obj) : obj;
        if (!nestedObjPtr) {
            YA_CORE_WARN("ReflectionSerializer: Cannot get mutable address for nested object '{}'", prop.getTypeName());
            return;
        }

        // 遍历 JSON 中的所有字段
        for (auto it = j.begin(); it != j.end(); ++it) {
            const std::string &jsonKey   = it.key();
            const auto        &jsonValue = it.value();

            auto *subProp = classPtr->getProperty(jsonKey);
            if (!subProp) {
                YA_CORE_WARN("ReflectionSerializer: Property '{}.{}' not found", classPtr->_name, jsonKey);
                continue;
            }

            try {
                if (is_base_type(subProp->typeIndex))
                {
                    deserializeAnyValue(*subProp, nestedObjPtr, jsonValue);
                }
                else {
                    // 递归反序列化嵌套对象
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
    }


    // /**
    //  * 序列化外部反射类型（非侵入式）
    //  */
    // template <typename T>
    //     requires serializer_detail::has_external_reflect_v<T>
    // static nlohmann::json serializeExternal(const T &obj)
    // {
    //     nlohmann::json j;

    //     auto visitor = [&j](const char *name, auto &value) {
    //         using ValueType = std::decay_t<decltype(value)>;

    //         if constexpr (requires { value.__visit_properties(std::declval<void (*)(const char *, int &)>()); }) {
    //             j[name] = serialize(value);
    //         }
    //         else if constexpr (serializer_detail::has_external_reflect_v<ValueType>) {
    //             j[name] = serializeExternal(value);
    //         }
    //         else {
    //             j[name] = value;
    //         }
    //     };

    //     T &mutableObj = const_cast<T &>(obj); // NOLINT(cppcoreguidelines-pro-type-const-cast)
    //     ::ya::reflection::detail::ExternalReflect<T>::visit_properties(mutableObj, visitor);

    //     return j;
    // }


    /**
     * 反序列化外部反射类型（非侵入式）
     */
    template <typename T>
        requires serializer_detail::has_external_reflect_v<T>
    static T deserializeExternal(const nlohmann::json &j)
    {
        T obj{};

        auto visitor = [&j](const char *name, auto &value) {
            if (!j.contains(name)) return;

            using ValueType = std::decay_t<decltype(value)>;

            if constexpr (requires { value.__visit_properties(std::declval<void (*)(const char *, int &)>()); }) {
                value = deserializeByRuntimeReflection<ValueType>(j[name]);
            }
            else if constexpr (serializer_detail::has_external_reflect_v<ValueType>) {
                value = deserializeExternal<ValueType>(j[name]);
            }
            else {
                value = j[name].get<ValueType>();
            }
        };

        ::ya::reflection::detail::ExternalReflect<T>::visit_properties(obj, visitor);

        return obj;
    }

    /**
     * 就地反序列化外部反射类型（非侵入式）
     */
    template <typename T>
        requires serializer_detail::has_external_reflect_v<T>
    static void deserializeInPlaceExternal(T &obj, const nlohmann::json &j)
    {
        auto visitor = [&j](const char *name, auto &value) {
            if (!j.contains(name)) return;

            using ValueType = std::decay_t<decltype(value)>;

            if constexpr (requires { value.__visit_properties(std::declval<void (*)(const char *, int &)>()); }) {
                deserializeInPlace(value, j[name]);
            }
            else if constexpr (serializer_detail::has_external_reflect_v<ValueType>) {
                deserializeInPlaceExternal(value, j[name]);
            }
            else {
                value = j[name].get<ValueType>();
            }
        };

        ::ya::reflection::detail::ExternalReflect<T>::visit_properties(obj, visitor);
    }

  private:

    // MARK: helper
    // ========================================================================
    // 辅助函数：处理 std::any 的序列化和反序列化
    // ========================================================================

    /**
     * 序列化 std::any 值到 JSON
     */
    static nlohmann::json serializeBasicAnyValue(const std::any &value, const Property &prop)
    {
        // 使用 typeIndex 来判断类型
        auto typeIdx = prop.typeIndex;



        // 基础类型
        if (typeIdx == refl::TypeIndex<int>::value()) {
            return std::any_cast<int>(value);
        }
        if (typeIdx == refl::TypeIndex<float>::value()) {
            return std::any_cast<float>(value);
        }
        if (typeIdx == refl::TypeIndex<double>::value()) {
            return std::any_cast<double>(value);
        }
        if (typeIdx == refl::TypeIndex<bool>::value()) {
            return std::any_cast<bool>(value);
        }
        if (typeIdx == refl::TypeIndex<std::string>::value()) {
            return std::any_cast<std::string>(value);
        }

        // TODO: 支持嵌套对象的递归序列化
        // 通过 addressGetter 获取成员地址，然后递归序列化
        // 这需要知道成员的类型名称，可以从 prop.metadata 或其他方式获取

        YA_CORE_WARN("ReflectionSerializer: Unsupported type for property (typeIndex: {})", typeIdx);
        return nlohmann::json();
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
    };

    /**
     * 反序列化 JSON 值到对象属性
     */
    static void deserializeAnyValue(const Property &prop, void *obj, const nlohmann::json &plainValue)
    {
        auto        typeIdx        = prop.typeIndex;
        static auto intTypeIndx    = ya::type_index_v<int>;
        static auto floatTypeIndx  = ya::type_index_v<float>;
        static auto doubleTypeIndx = ya::type_index_v<double>;
        static auto boolTypeIndx   = ya::type_index_v<bool>;
        static auto stringTypeIndx = ya::type_index_v<std::string>;



        // 基础类型
        if (typeIdx == intTypeIndx) {
            prop.setter(obj, plainValue.get<int>());
            return;
        }
        if (typeIdx == floatTypeIndx) {
            prop.setter(obj, plainValue.get<float>());
            return;
        }
        if (typeIdx == doubleTypeIndx) {
            prop.setter(obj, plainValue.get<double>());
            return;
        }
        if (typeIdx == boolTypeIndx) {
            prop.setter(obj, plainValue.get<bool>());
            return;
        }
        if (typeIdx == stringTypeIndx) {
            prop.setter(obj, plainValue.get<std::string>());
            return;
        }

        // TODO: 支持嵌套对象的递归反序列化
        // 通过 addressGetterMutable 获取成员地址，然后递归反序列化

        YA_CORE_WARN("ReflectionSerializer: Unsupported type for property (typeIndex: {})", typeIdx);
    }
};

} // namespace ya
