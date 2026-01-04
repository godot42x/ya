/**
 * @brief 统一反射宏 - 一个宏解决所有反射需求
 *
 * 使用 YA_REFLECT 可以：
 * 1. 侵入式反射（类内部）
 * 2. 非侵入式反射（类外部，适用于第三方库）
 * 3. 自动注册元数据
 * 4. 自动注册序列化器
 * 5. 自动注册类型信息
 *
 * 当前限制：
 * - 最多支持 16 个属性（通过可变参宏实现）
 * - 如果类成员超过 16 个，建议：
 *   A. 重构为多个小类（单一职责原则）
 *   B. 使用代码生成器 + 注解系统
 *   C. 等待 C++26 Static Reflection (P2996)
 *
 * 未来方向：
 * - 转向代码生成器方案（推荐 Clang LibTooling）
 * - 配合 [[reflect]] 等自定义属性标记
 * - 自动生成反射代码，无宏限制
 */

#pragma once

#include "Core/Base.h"
#include "Core/Macro/VariadicMacros.h" // 通用可变参宏工具

#include "Core/Reflection/RuntimeReflectionBridge.h"
#include "Core/Serialization/SerializerRegistry.h"

#include <functional>
#include <type_traits>


namespace ya::reflection
{

// ============================================================================
// 统一反射系统 - 一个宏搞定一切
// ============================================================================

/**
 * 侵入式用法（在类内部）：
 *
 * struct PlayerComponent {
 *     YA_REFLECT(PlayerComponent,
 *         PROP(health, .EditAnywhere().Range(0, 100)),
 *         PROP(speed, .EditAnywhere()),
 *         PROP(cache, .NotSerialized())
 *     )
 *
 *     float health = 100.0f;
 *     float speed = 5.0f;
 *     float cache = 0.0f;
 * };
 *
 * 非侵入式用法（在类外部，用于第三方库）：
 *
 * // 第三方库的类
 * namespace ThirdParty {
 *     struct Vector3 {
 *         float x, y, z;
 *     };
 * }
 *
 * // 在外部注册反射
 * YA_REFLECT_EXTERNAL(ThirdParty::Vector3,
 *     PROP(x, .EditAnywhere()),
 *     PROP(y, .EditAnywhere()),
 *     PROP(z, .EditAnywhere())
 * )
 */

// 辅助宏：从 PROP(name, .meta...) 中提取属性名
#define YA_META_PROP_NAME(name, ...) name

// 辅助宏：从 PROP(name, .meta...) 中提取元数据
#define YA_META_PROP_META(name, ...) __VA_ARGS__

// ============================================================================
// 反射专用操作宏 - 基于通用可变参宏系统 (VariadicMacros.h)
// ============================================================================

// 操作1：注册属性（运行时反射 + 元数据）
#define YA_REGISTER_PROPERTY(ClassName, prop)                        \
    _runtimeRegistrar.property(YA_STRINGIFY(YA_META_PROP_NAME prop), \
                               &ClassName::YA_META_PROP_NAME   prop, \
                               MetaBuilder() YA_META_PROP_META prop);

// 操作2：访问属性（侵入式，直接访问成员）
#define YA_VISIT_PROPERTY(Type, prop) \
    visitor(YA_STRINGIFY(YA_META_PROP_NAME prop), YA_META_PROP_NAME prop);

// 操作3：访问属性（外部反射，通过对象访问）
#define YA_VISIT_PROPERTY_EXT(Type, prop) \
    visitor(YA_STRINGIFY(YA_META_PROP_NAME prop), __obj.YA_META_PROP_NAME prop);

// ============================================================================
// 用户接口宏 - 支持 1-16 个属性（基于 YA_FOREACH）
// TODO: 支持有上限，拓展宏也没必要， 或许可以用:
// REFLECT_BEGIN(Type)
//    REFLECT_PROP(prop1)
//    REFLECT_PROP(prop2)
// REFLECT_END()
// 这样就没有上限了
// 此外，未来可以考虑代码生成器方案
// 目前16个属性已经够用了
// ============================================================================

/// 注册属性列表
#define YA_REGISTER_PROPS(Type, ...) YA_FOREACH(YA_REGISTER_PROPERTY, Type, __VA_ARGS__)
/// 访问属性列表
#define YA_VISIT_PROPS(Type, ...) YA_FOREACH(YA_VISIT_PROPERTY, Type, __VA_ARGS__)
/// 访问属性列表
#define YA_VISIT_PROPS_EXT(Type, ...) YA_FOREACH(YA_VISIT_PROPERTY_EXT, Type, __VA_ARGS__)

// ============================================================================
// 核心宏：YA_REFLECT - 侵入式反射（类内部使用）
// ============================================================================

#define YA_REFLECT(ClassName, ...)                                                                     \
  private:                                                                                             \
    static inline const char *__type_name = #ClassName;                                                \
    static inline ::ya::reflection::detail::AutoReflectRegistrar<ClassName>                            \
        __auto_reflect_##ClassName{#ClassName};                                                        \
    struct _MetaRegistrar_##ClassName                                                                  \
    {                                                                                                  \
        static constexpr const char                                    *_className = #ClassName;       \
        ::ya::reflection::detail::RuntimeReflectionRegistrar<ClassName> _runtimeRegistrar{#ClassName}; \
        _MetaRegistrar_##ClassName()                                                                   \
        {                                                                                              \
            using namespace ::ya::reflection;                                                          \
            /* 一次性注册：运行时反射 + 元数据 */                                                      \
            YA_REGISTER_PROPS(ClassName, __VA_ARGS__)                                                  \
            /* 注册 ECS 序列化器 (使用 visitor 模式) */                                                \
            /* TODO: 从宏里面移除*/                                                                    \
            if constexpr (std::is_base_of_v<::ya::IComponent, ClassName>) {                            \
                ::ya::reflection::detail::registerECSSerializer<ClassName>(#ClassName);                \
            }                                                                                          \
        }                                                                                              \
    };                                                                                                 \
    static inline _MetaRegistrar_##ClassName _meta_reg_##ClassName;                                    \
                                                                                                       \
  public:                                                                                              \
    static const char *getTypeName() { return __type_name; }                                           \
    template <typename __ReflectVisitor>                                                               \
    void __visit_properties(__ReflectVisitor &&visitor)                                                \
    {                                                                                                  \
        YA_VISIT_PROPS(ClassName, __VA_ARGS__)                                                         \
    }

// ============================================================================
// 核心宏：YA_REFLECT_EXTERNAL - 非侵入式反射（类外部使用）
// ============================================================================

#define YA_REFLECT_EXTERNAL(ClassName, ...)                                                                \
    namespace ya::reflection::detail                                                                       \
    {                                                                                                      \
    template <>                                                                                            \
    struct ExternalReflect<ClassName>                                                                      \
    {                                                                                                      \
        static constexpr const char                  *type_name = #ClassName;                              \
        static inline AutoReflectRegistrar<ClassName> registrar{#ClassName};                               \
        struct _MetaRegistrar                                                                              \
        {                                                                                                  \
            static constexpr const char                                    *_className = #ClassName;       \
            ::ya::reflection::detail::RuntimeReflectionRegistrar<ClassName> _runtimeRegistrar{#ClassName}; \
            _MetaRegistrar()                                                                               \
            {                                                                                              \
                YA_PROFILE_STATIC_INIT(#ClassName);                                                        \
                using namespace ::ya::reflection;                                                          \
                /* 一次性注册：运行时反射 + 元数据 */                                                      \
                YA_REGISTER_PROPS(ClassName, __VA_ARGS__)                                                  \
            }                                                                                              \
        };                                                                                                 \
        static inline _MetaRegistrar meta_reg;                                                             \
        template <typename __ReflectVisitor>                                                               \
        static void visit_properties(ClassName &__obj, __ReflectVisitor &&visitor)                         \
        {                                                                                                  \
            YA_VISIT_PROPS_EXT(ClassName, __VA_ARGS__)                                                     \
        }                                                                                                  \
    };                                                                                                     \
    }

// ============================================================================
// 备选方案：BEGIN/END 宏 - 无参数数量限制（推荐用于大型类）
// ============================================================================

/**
 * 使用方式：
 * 
 * struct LargeComponent {
 *     YA_REFLECT_BEGIN(LargeComponent)
 *         YA_REFLECT_FIELD(field1, .EditAnywhere())
 *         YA_REFLECT_FIELD(field2, .Range(0, 100))
 *         YA_REFLECT_FIELD(field3, .NotSerialized())
 *         // ... 可以添加任意多个字段，没有数量限制
 *     YA_REFLECT_END(LargeComponent)
 * 
 *     int field1 = 0;
 *     float field2 = 50.0f;
 *     std::string field3;
 * };
 * 
 * 优点：
 * - 无数量限制
 * - 语法清晰
 * - 易于扩展
 * 
 * 缺点：
 * - 需要手动开始和结束
 * - visitor 需要单独定义
 */

#define YA_REFLECT_BEGIN(ClassName)                                                                    \
  private:                                                                                             \
    static inline const char *__type_name = #ClassName;                                                \
    static inline ::ya::reflection::detail::AutoReflectRegistrar<ClassName>                            \
        __auto_reflect_##ClassName{#ClassName};                                                        \
    struct _MetaRegistrar_##ClassName                                                                  \
    {                                                                                                  \
        using _ReflectClass = ClassName;                                                               \
        static constexpr const char                                    *_className = #ClassName;       \
        ::ya::reflection::detail::RuntimeReflectionRegistrar<ClassName> _runtimeRegistrar{#ClassName}; \
        _MetaRegistrar_##ClassName()                                                                   \
        {                                                                                              \
            using namespace ::ya::reflection;                                                          \
            /* 一次性注册：运行时反射 + 元数据 */

#define YA_REFLECT_FIELD(FieldName, ...)                                                  \
    _runtimeRegistrar.property(#FieldName,                                                \
                               &_ReflectClass::FieldName,                                 \
                               MetaBuilder() __VA_ARGS__);

#define YA_REFLECT_END(ClassName)                                                                      \
            /* 注册 ECS 序列化器 (使用 visitor 模式) */                                                \
            /* TODO: 从宏里面移除*/                                                                    \
            if constexpr (std::is_base_of_v<::ya::IComponent, ClassName>) {                            \
                ::ya::reflection::detail::registerECSSerializer<ClassName>(#ClassName);                \
            }                                                                                          \
        }                                                                                              \
    };                                                                                                 \
    static inline _MetaRegistrar_##ClassName _meta_reg_##ClassName;                                    \
    /* Visitor 访问列表 - 需要手动维护 */                                                             \
    struct _VisitorFields_##ClassName                                                                  \
    {                                                                                                  \
        template <typename __ReflectVisitor>                                                           \
        static void visit(ClassName* self, __ReflectVisitor&& visitor)                                 \
        {

#define YA_VISIT_FIELD(FieldName) \
    visitor(#FieldName, self->FieldName);

#define YA_REFLECT_VISITOR_END(ClassName)                                                              \
        }                                                                                              \
    };                                                                                                 \
  public:                                                                                              \
    static const char *getTypeName() { return __type_name; }                                           \
    template <typename __ReflectVisitor>                                                               \
    void __visit_properties(__ReflectVisitor &&visitor)                                                \
    {                                                                                                  \
        _VisitorFields_##ClassName::visit(this, std::forward<__ReflectVisitor>(visitor));             \
    }

// ============================================================================
// 外部反射 BEGIN/END 宏（用于第三方类型）
// ============================================================================

#define YA_REFLECT_EXTERNAL_BEGIN(ClassName)                                                               \
    namespace ya::reflection::detail                                                                       \
    {                                                                                                      \
    template <>                                                                                            \
    struct ExternalReflect<ClassName>                                                                      \
    {                                                                                                      \
        using _ReflectClass = ClassName;                                                                   \
        static constexpr const char                  *type_name = #ClassName;                              \
        static inline AutoReflectRegistrar<ClassName> registrar{#ClassName};                               \
        struct _MetaRegistrar                                                                              \
        {                                                                                                  \
            static constexpr const char                                    *_className = #ClassName;       \
            ::ya::reflection::detail::RuntimeReflectionRegistrar<ClassName> _runtimeRegistrar{#ClassName}; \
            _MetaRegistrar()                                                                               \
            {                                                                                              \
                YA_PROFILE_STATIC_INIT(#ClassName);                                                        \
                using namespace ::ya::reflection;                                                          \
                /* 一次性注册：运行时反射 + 元数据 */

#define YA_REFLECT_EXTERNAL_FIELD(FieldName, ...)                                         \
    _runtimeRegistrar.property(#FieldName,                                                \
                               &_ReflectClass::FieldName,                                 \
                               MetaBuilder() __VA_ARGS__);

#define YA_REFLECT_EXTERNAL_END(ClassName)                                                             \
            }                                                                                          \
        };                                                                                             \
        static inline _MetaRegistrar meta_reg;                                                         \
        template <typename __ReflectVisitor>                                                           \
        static void visit_properties(ClassName &__obj, __ReflectVisitor &&visitor)                     \
        {

#define YA_VISIT_EXTERNAL_FIELD(FieldName) \
    visitor(#FieldName, __obj.FieldName);

#define YA_REFLECT_EXTERNAL_VISITOR_END(ClassName)                                                     \
        }                                                                                              \
    };                                                                                                 \
    }

// ============================================================================
// 辅助模板和宏
// ============================================================================

namespace detail
{
// 外部反射模板（默认实现）
template <typename T>
struct ExternalReflect
{
    static constexpr bool has_external_reflection = false;
};

// 自动注册器
template <typename T>
struct AutoReflectRegistrar
{
    AutoReflectRegistrar(const char *name)
    {
        // 注册类型名称到全局注册表
        // 这里可以添加运行时类型注册逻辑
    }
};

// ECS 序列化器注册函数（实现在 ECSSerializerHelper.h）
template <typename ComponentType>
void registerECSSerializer(const std::string &typeName);
} // namespace detail

// 宏展开辅助
#define YA_EXPAND(...) __VA_ARGS__

// PROP 宏：用于定义属性及其元数据
#define PROP(name, ...) (name, __VA_ARGS__)

} // namespace ya::reflection



// #include "Core/Reflection/ECSSerializerHelper.h"
#include "Core/Profiling/StaticInitProfiler.h"
#include "ECS/Component.h"
