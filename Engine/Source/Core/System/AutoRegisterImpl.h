#pragma once

// 此文件包含 AutoRegisterHelper 的模板实现
// 被 AutoRegister.h 包含

#include "Core/Serialization/ReflectionSerializer.h"
#include "Core/Serialization/SerializerRegistry.h"
#include "Core/System/AutoRegister.h"
#include <entt/entt.hpp>
#include <nlohmann/json.hpp>

namespace ya
{

template <typename T>
inline AutoRegisterHelper<T>::AutoRegisterHelper(const char *typeName)
{
    // 编译时检查：确保类型 T 有 static void registerReflection() 方法
    static constexpr bool ok = requires { T::registerReflection(); };
    if constexpr (!ok) {
        static_assert(false, "Component must have 'static void registerReflection()' method");
    }

    // 注册反射到 TypeRegistry（通过辅助函数避免循环依赖）
    registerReflectionToTypeRegistry([]() {
        T::registerReflection();
    });

    // 注册 ECS 组件序列化器
    ECSSerializerRegistry::get().registerSerializer(
        typeName,
        // Serializer
        [typeName](::entt::registry &registry, ::entt::entity entity, nlohmann::json &components) {
            if (registry.all_of<T>(entity))
            {
                auto &comp           = registry.get<T>(entity);
                components[typeName] = ReflectionSerializer::serialize(comp);
            }
        },
        // Deserializer
        [](::entt::registry &registry, ::entt::entity entity, const nlohmann::json &j) {
            auto comp = ReflectionSerializer::deserialize<T>(j);
            registry.emplace<T>(entity, comp);
        });
}

} // namespace ya
