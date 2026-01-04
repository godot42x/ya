#pragma once

#include "Core/Reflection/ReflectionSerializer.h"
#include "Core/Serialization/SerializerRegistry.h"
#include <entt/entt.hpp>
#include <nlohmann/json.hpp>
#include <string>

namespace ya::reflection::detail
{

/**
 * @brief ECS 序列化器自动注册辅助函数
 *
 * 使用 visitor 模式自动序列化/反序列化组件
 */
template <typename ComponentType>
inline void registerECSSerializer(const std::string &typeName)
{
    ::ya::ECSSerializerRegistry::get().registerSerializer(
        typeName,
        // Serializer: 使用 ReflectionSerializer 自动序列化
        [typeName](::entt::registry &registry, ::entt::entity entity, ::nlohmann::json &components) {
            if (registry.all_of<ComponentType>(entity)) {
                auto &comp           = registry.get<ComponentType>(entity);
                components[typeName] = ::ya::ReflectionSerializer::serialize(comp);
            }
        },
        // Deserializer: 使用 ReflectionSerializer 自动反序列化
        [](::entt::registry &registry, ::entt::entity entity, const ::nlohmann::json &j) {
            auto &comp = registry.emplace_or_replace<ComponentType>(entity);
            ::ya::ReflectionSerializer::deserializeInPlace(comp, j);
        });
}

} // namespace ya::reflection::detail
