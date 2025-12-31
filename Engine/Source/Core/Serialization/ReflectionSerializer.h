
#pragma once

#include "Core/Base.h"

#include <entt/entt.hpp>
#include <nlohmann/json.hpp>

#include "SerializerHelper.h"


namespace ya
{

/**
 * @brief 通过反射系统自动序列化组件
 */
struct ReflectionSerializer
{
    /**
     * 通过反射将对象序列化为 JSON
     */
    template <typename T>
        requires(!std::is_pointer_v<T>)
    static nlohmann::json serialize(const T &obj)
    {
        nlohmann::json j;

        // 获取反射信息
        std::string className = typeid(T).name();
        uint32_t    typeIndex = ya::type_index_v<T>;
        auto       *classInfo = ClassRegistry::instance().getClass(typeIndex);

        if (!classInfo) {
            YA_CORE_WARN("No reflection info for class: {}", className);
            return j;
        }

        // 遍历所有属性
        for (const auto &[propName, prop] : classInfo->properties) {
            try {
                std::any value = prop.getter(const_cast<T *>(&obj));
                j[propName]    = SerializerHelper::anyToJson(value, prop.typeIndex);
            }
            catch (const std::exception &e) {
                YA_CORE_WARN("Failed to serialize property {}: {}", propName, e.what());
            }
        }
        return j;
    }


    /**
     * 从 JSON 反序列化对象
     */
    template <typename T>
    static T deserialize(const nlohmann::json &j)
    {
        T obj{};

        auto  typeName  = typeid(T).name();
        auto *classInfo = ClassRegistry::instance().getClass(type_index_v<T>);

        if (!classInfo) {
            YA_CORE_ERROR("No reflection info for class: {}", typeName);
            return obj;
        }

        for (auto &[key, value] : j.items()) {
            auto it = classInfo->properties.find(key);
            if (it == classInfo->properties.end()) continue;

            const Property &prop = it->second;
            if (!prop.setter) continue;

            try {
                std::any anyValue = SerializerHelper::jsonToAny(value, prop.typeHash);
                prop.setter(&obj, anyValue);
            }
            catch (const std::exception &e) {
                YA_CORE_WARN("Failed to deserialize property {}: {}", key, e.what());
            }
        }

        return obj;
    }
};


} // namespace ya