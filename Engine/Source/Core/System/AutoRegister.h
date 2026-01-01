#pragma once

#include <functional>

namespace ya
{

// 前置声明
class TypeRegistry;
class ECSSerializerRegistry;

/**
 * @brief 自动注册器 - 使用静态初始化实现零配置组件注册
 *
 * 已重构：功能合并到 TypeRegistry，此文件仅保留宏定义和辅助类
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

/**
 * @brief 辅助函数：将反射注册转发到 TypeRegistry
 * 
 * 声明在这里，实现在 AutoRegister.cpp，避免循环依赖
 */
void registerReflectionToTypeRegistry(std::function<void()> registrar);

/**
 * @brief 自动注册助手 - 利用静态成员初始化触发注册
 *
 * 要求：T 必须提供 static void registerReflection() 方法
 */
template <typename T>
struct AutoRegisterHelper
{
    AutoRegisterHelper(const char *typeName);
};

} // namespace ya

// ============================================================================
// 宏定义
// ============================================================================

#define REFLECT_AUTO_REGISTER(ClassName)                                                          \
  private:                                                                                        \
    static inline ::ya::AutoRegisterHelper<ClassName> __auto_register_##ClassName##_{#ClassName}; \
                                                                                                  \
  public:

#define YA_ECS_COMPONENT(ComponentType) \
    REFLECT_AUTO_REGISTER(ComponentType)

// ============================================================================
// 模板实现（需要放在头文件中）
// ============================================================================

#include "AutoRegisterImpl.h"
