#pragma once

#include <functional>
#include <vector>

namespace ya
{

/**
 * @brief 自动注册器 - 使用静态初始化实现零配置组件注册
 *
 * 使用方法：
 * 1. 在组件类中添加：REFLECT_AUTO_REGISTER(ComponentName)
 * 2. 定义 static void registerReflection() 函数
 *
 * 示例:
 * struct TransformComponent : public IComponent {
 *     REFLECT_AUTO_REGISTER(TransformComponent)
 *
 *     glm::vec3 _position;
 *
 *     static void registerReflection() {
 *         Register<TransformComponent>("TransformComponent")
 *             .property("position", &TransformComponent::_position);
 *     }
 * };
 */

class AutoRegisterRegistry
{
  public:
    using RegisterFunc = std::function<void()>;

    static AutoRegisterRegistry &get();

    void addRegisterFunc(RegisterFunc func)
    {
        _registerFuncs.push_back(std::move(func));
    }

    void executeAll()
    {
        for (auto &func : _registerFuncs)
        {
            func();
        }
    }

  private:
    std::vector<RegisterFunc> _registerFuncs;
};

// 前置声明
class ECSSerializerRegistry;

/**
 * @brief 自动注册助手 - 利用静态成员初始化触发注册
 *
 * 当类的静态成员被初始化时，会自动将 registerReflection() 函数
 * 和序列化注册添加到全局注册表中
 *
 * 要求：T 必须提供 static void registerReflection() 方法
 *
 * 注意：不使用 concept 约束模板参数，因为在宏展开时类还未完全定义
 * 编译时检查会在构造函数中进行（此时类已完全定义）
 */
template <typename T>
struct AutoRegisterHelper
{
    AutoRegisterHelper(const char *typeName);
};

} // namespace ya


#define REFLECT_AUTO_REGISTER(ClassName)                                                          \
  private:                                                                                        \
    static inline ::ya::AutoRegisterHelper<ClassName> __auto_register_##ClassName##_{#ClassName}; \
                                                                                                  \
  public:

#define YA_ECS_COMPONENT(ComponentType) \
    REFLECT_AUTO_REGISTER(ComponentType)


// ============================================================================
// 模板实现 - 必须在头文件中定义以避免链接错误
// ============================================================================

// 包含必要的头文件（在 ya 命名空间外）
#include "Core/Serialization/ReflectionSerializer.h"
#include "Core/Serialization/SerializerRegistry.h"
#include <entt/entt.hpp>
#include <nlohmann/json.hpp>


namespace ya
{

template <typename T>
inline AutoRegisterHelper<T>::AutoRegisterHelper(const char *typeName)
{
    // 编译时检查：确保类型 T 有 static void registerReflection() 方法
    // 此时类已完全定义，可以正确检查
    static constexpr bool ok = requires { T::registerReflection(); };
    if constexpr (!ok) {
        static_assert(false, "Component must have 'static void registerReflection()' method");
    }

    // 注册反射
    AutoRegisterRegistry::get().addRegisterFunc([]() {
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
