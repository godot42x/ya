
#pragma once

#include "Core/Base.h"
#include <entt/entt.hpp>
#include <nlohmann/json.hpp>

#include "SerializerHelper.h"
#include <glm/glm.hpp>


/**
 * @brief 组件序列化框架 - 基于 ECS + 反射
 *
 * 设计目标:
 * 1. 自动通过反射序列化组件
 * 2. 支持手动序列化（toJson/fromJson）
 * 3. 类型安全的序列化/反序列化
 */

namespace ya
{

// ============================================================================
// 序列化宏
// ============================================================================

/**
 * 在组件中声明此宏以启用自动序列化
 */
#define YA_COMPONENT_SERIALIZABLE() \
    nlohmann::json toJson() const;  \
    static auto    fromJson(const nlohmann::json &j) -> std::decay_t<decltype(*this)>;


// ============================================================================
// 反射序列化适配器
// ============================================================================

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
        uint32_t    typeIndex = type_index_v<T>;
        auto       *classInfo = ClassRegistry::instance().getClass(typeIndex);

        if (!classInfo) {
            YA_CORE_WARN("No reflection info for class: {}", className);
            return j;
        }

        // 遍历所有属性
        for (const auto &[propName, prop] : classInfo->properties) {
            try {
                std::any value = prop.getter(const_cast<T *>(&obj));
                j[propName]    = anyToJson(value, prop.typeIndex);
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
                std::any anyValue = jsonToAny(value, prop.typeHash);
                prop.setter(&obj, anyValue);
            }
            catch (const std::exception &e) {
                YA_CORE_WARN("Failed to deserialize property {}: {}", key, e.what());
            }
        }

        return obj;
    }

  private:
    /**
     * 将 std::any 转换为 JSON
     * 根据类型索引判断类型
     */
    static nlohmann::json anyToJson(const std::any &value, uint32_t typeIndex)
    {
        // 基础类型
        if (typeIndex == type_index_v<int>) {
            return std::any_cast<int>(value);
        }
        else if (typeIndex == type_index_v<float>) {
            return std::any_cast<float>(value);
        }
        else if (typeIndex == type_index_v<double>) {
            return std::any_cast<double>(value);
        }
        else if (typeIndex == type_index_v<bool>) {
            return std::any_cast<bool>(value);
        }
        else if (typeIndex == type_index_v<std::string>) {
            return std::any_cast<std::string>(value);
        }
        // glm 类型
        else if (typeIndex == type_index_v<glm::vec2>) {
            return SerializerHelper::toJson(std::any_cast<glm::vec2>(value));
        }
        else if (typeIndex == type_index_v<glm::vec3>) {
            return SerializerHelper::toJson(std::any_cast<glm::vec3>(value));
        }
        else if (typeIndex == type_index_v<glm::vec4>) {
            return SerializerHelper::toJson(std::any_cast<glm::vec4>(value));
        }
        else if (typeIndex == type_index_v<glm::mat4>) {
            return SerializerHelper::toJson(std::any_cast<glm::mat4>(value));
        }

        YA_CORE_WARN("Unknown type index for serialization: {}", typeIndex);
        return nullptr;
    }

    /**
     * 将 JSON 转换为 std::any
     */
    static std::any jsonToAny(const nlohmann::json &j, size_t typeHash)
    {
        // 基础类型
        if (typeHash == typeid(int).hash_code()) {
            return std::any(j.get<int>());
        }
        else if (typeHash == typeid(float).hash_code()) {
            return std::any(j.get<float>());
        }
        else if (typeHash == typeid(double).hash_code()) {
            return std::any(j.get<double>());
        }
        else if (typeHash == typeid(bool).hash_code()) {
            return std::any(j.get<bool>());
        }
        else if (typeHash == typeid(std::string).hash_code()) {
            return std::any(j.get<std::string>());
        }
        // glm 类型
        else if (typeHash == typeid(glm::vec2).hash_code()) {
            return std::any(SerializerHelper::toVec2(j));
        }
        else if (typeHash == typeid(glm::vec3).hash_code()) {
            return std::any(SerializerHelper::toVec3(j));
        }
        else if (typeHash == typeid(glm::vec4).hash_code()) {
            return std::any(SerializerHelper::toVec4(j));
        }
        else if (typeHash == typeid(glm::mat4).hash_code()) {
            return std::any(SerializerHelper::toMat4(j));
        }

        YA_CORE_ERROR("Unknown type hash for deserialization: {}", typeHash);
        return std::any();
    }
};


} // namespace ya