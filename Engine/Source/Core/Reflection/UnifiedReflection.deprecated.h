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


#include <functional>
#include <string>
#include <type_traits>
#include <vector>



#include "Core/Profiling/StaticInitProfiler.h"
#include "Core/Reflection/ReflectionSerializer.h"
#include "Core/Reflection/RuntimeReflectionBridge.h"
#include "Core/Serialization/SerializerRegistry.h"



// ============================================================================
// 辅助模板和宏
// ============================================================================

namespace ya::reflection
{
namespace detail
{
// 外部反射模板（默认实现）
template <typename T>
struct ExternalReflect
{
    static constexpr bool has_external_reflection = false;
};



/**
 * @brief ECS 序列化器自动注册辅助函数
 *
 * 优先使用 Registry 模式，兼容 Visitor 模式
 * 支持所有注册到运行时反射系统的类型
 */
template <typename Type>
void registerECSSerializer(const std::string &typeName)
{
    ::ya::ECSSerializerRegistry::get().registerSerializer(
        typeName,
        // Serializer: 优先使用 Registry 模式
        [typeName](::entt::registry &registry, ::entt::entity entity, ::nlohmann::json &components) {
            if (registry.all_of<Type>(entity)) {
                auto &comp = registry.get<Type>(entity);

                // 优先使用 Registry 模式（支持所有反射类型）
                components[typeName] = ::ya::ReflectionSerializer::serializeProperty(comp, typeName);
            }
        },
        // Deserializer: 优先使用 Registry 模式
        [typeName](::entt::registry &registry, ::entt::entity entity, const ::nlohmann::json &j) {
            auto &comp = registry.emplace_or_replace<Type>(entity);

            // 优先使用 Registry 模式（支持所有反射类型）
            ::ya::ReflectionSerializer::deserializeByRuntimeReflection(comp, j, typeName);
        });
}



} // namespace detail

} // namespace ya::reflection



#if 0 // 注释掉，使用 BEGIN/END 宏代替


    #include "Core/Macro/VariadicMacros.h" // 通用可变参宏工具
// ============================================================================
// 统一反射系统 - 一个宏搞定一切
// ============================================================================



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
                                   &ClassName::YA_META_PROP_NAME prop,   \
                                   YA_EXPAND(MetaBuilder() YA_META_PROP_META prop));

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
// ⚠️ 已废弃：请使用 YA_REFLECT_BEGIN/END 宏
// ============================================================================

    #define YA_REFLECT(ClassName, ...)                                                                     \
      private:                                                                                             \
        static inline const char *__type_name = #ClassName;                                                \
        struct _MetaRegistrar_##ClassName                                                                  \
        {                                                                                                  \
            static constexpr const char                                    *_className = #ClassName;       \
            ::ya::reflection::detail::RuntimeReflectionRegistrar<ClassName> _runtimeRegistrar{#ClassName}; \
            _MetaRegistrar_##ClassName()                                                                   \
            {                                                                                              \
                using namespace ::ya::reflection;                                                          \
                /* 一次性注册：运行时反射 + 元数据 */                                                      \
                YA_REGISTER_PROPS(ClassName, __VA_ARGS__)                                                  \
                /* TODO: 从宏里面移除*/                                                                    \
                ::ya::reflection::detail::registerECSSerializer<Type>(#ClassName);                         \
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

#endif
// ============================================================================
// 备选方案：BEGIN/END 宏 - 无参数数量限制（推荐用于大型类）
// ============================================================================


#define YA_REFLECT_EXTENSION(TypeName) \
    ::ya::reflection::detail::registerECSSerializer<TypeName>(#TypeName);


#define YA_REFLECT_BEGIN(ClassName)                                                                    \
  private:                                                                                             \
    static inline const char *__type_name = #ClassName;                                                \
    struct _MetaRegistrar_##ClassName;                                                                 \
    using _RegisterType = _MetaRegistrar_##ClassName;                                                  \
    struct _MetaRegistrar_##ClassName                                                                  \
    {                                                                                                  \
        using _ReflectClass                                                        = ClassName;        \
        static constexpr const char                                    *_className = #ClassName;       \
        ::ya::reflection::detail::RuntimeReflectionRegistrar<ClassName> _runtimeRegistrar{#ClassName}; \
        _MetaRegistrar_##ClassName()                                                                   \
        {                                                                                              \
            using namespace ::ya::reflection;                                                          \
            YA_REFLECT_EXTENSION(ClassName)


// YA_REFLECT_FIELD: 同时注册到运行时反射和收集到 visitor 列表
#define YA_REFLECT_FIELD(FieldName, ...)                  \
    _runtimeRegistrar.property(#FieldName,                \
                               &_ReflectClass::FieldName, \
                               MetaBuilder() __VA_ARGS__);

// clang-format off
#define YA_REFLECT_END()                   \
        }                                  \
    }                                      \
    ;                                      \
    static inline _RegisterType _meta_reg{}; \
                                           \
  public:                                  \
    static const char *getTypeName() { return __type_name; }
  // clang-format on



// ============================================================================
// 外部反射 BEGIN/END 宏（用于第三方类型）
// ============================================================================

#define YA_REFLECT_EXTERNAL_BEGIN(ClassName)                                                               \
    namespace ya::reflection::detail                                                                       \
    {                                                                                                      \
    template <>                                                                                            \
    struct ExternalReflect<::ClassName>                                                                    \
    {                                                                                                      \
        using _ReflectClass                    = ::ClassName;                                              \
        static constexpr const char *type_name = #ClassName;                                               \
        struct _MetaRegistrar                                                                              \
        {                                                                                                  \
            static constexpr const char                                    *_className = #ClassName;       \
            ::ya::reflection::detail::RuntimeReflectionRegistrar<ClassName> _runtimeRegistrar{#ClassName}; \
            _MetaRegistrar()                                                                               \
            {                                                                                              \
                YA_PROFILE_STATIC_INIT(#ClassName);                                                        \
                using namespace ::ya::reflection;                                                          \
                YA_REFLECT_EXTENSION(ClassName)


#define YA_REFLECT_EXTERNAL_FIELD(FieldName, ...) YA_REFLECT_FIELD(FieldName, __VA_ARGS__)

// clang-format off
#define YA_REFLECT_EXTERNAL_END()          \
            }                                  \
        }                                      \
        ;                                      \
        static inline _MetaRegistrar meta_reg; \
    };                                         \
    } /* close ya::reflection::detail */
// clang-format on
