
#pragma once

// #include "Core/Reflection/UnifiedReflection.h"

#include "Core/Base.h"


#include <functional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>



#include "Core/Profiling/StaticInitProfiler.h"
#include "Core/Reflection/MetadataSupport.h"
#include "Core/Reflection/PropertyExtensions.h"
#include "Core/Reflection/ReflectionSerializer.h"
#include "Core/Reflection/RuntimeReflectionBridge.h"


// TODO: should not be in core?
#include "ECS/ECSRegistry.h"

namespace ya
{
struct IComponent;
}


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

template <typename T>
struct Visitor
{
    static void visit_properties(T &obj, auto &&visitor)
    {
        if constexpr (serializer_detail::has_external_reflect_v<T>) {
            ::ya::reflection::detail::ExternalReflect<T>::visit_properties(obj, std::forward<decltype(visitor)>(visitor));
        }
        else {
            obj.__visit_properties(std::forward<decltype(visitor)>(visitor));
        }
    }
};



/**
 * @brief ECS 序列化器自动注册辅助函数
 *
 * 优先使用 Registry 模式，兼容 Visitor 模式
 * 支持所有注册到运行时反射系统的类型
 */
template <typename Type>
void registerECSType(const std::string &typeName)
{
    // printf("Registering ECS Serializer for type: %s\n", typeName.c_str());
    // printf("Type Index: %u\n", ya::type_index_v<Type>);
    // printf("hash code: %zu\n", typeid(Type).hash_code());
    // inline static const uint32_t typeIndex = ya::type_index_v<Type>; always be 0 at static init time?
    // -> put after static init


    if constexpr (std::derived_from<Type, ::ya::IComponent>) {
        ::ya::ECSRegistry::get().registerComponent<Type>(
            typeName,
            // Serializer: 优先使用 Registry 模式
            [](::entt::registry &registry, ::entt::entity entity) -> void * {
                if (registry.all_of<Type>(entity)) {
                    return &registry.get<Type>(entity);
                }
                return nullptr;
            },
            [](::entt::registry &registry, ::entt::entity entity) -> void * {
                // 使用 emplace_or_replace 避免重复添加崩溃
                return &registry.emplace<Type>(entity);
            });
    }
}



} // namespace detail

} // namespace ya::reflection



// Extension hook for reflection registration.
// Users may override this macro BEFORE including this header.
// Notes:
// - Invoked inside a scope where `_ReflectClass` and `type_name` exist.
// - Keep extensions (profiling, ECS serializer, custom registry hooks) here.
#ifndef YA_REFLECT_EXTENSION
    #define YA_REFLECT_EXTENSION(type_name) \
        YA_PROFILE_STATIC_INIT(type_name);  \
        ::ya::reflection::detail::registerECSType<_ReflectClass>(type_name);
#endif


#define YA_REFLECT_BEGIN(ClassName)                                                                            \
  private:                                                                                                     \
    static inline const char *__type_name = #ClassName;                                                        \
    struct reflection_detail;                                                                                  \
    using _reflect_helper_class = reflection_detail;                                                           \
    struct reflection_detail                                                                                   \
    {                                                                                                          \
        using _ReflectClass                    = ClassName;                                                    \
        static constexpr const char *type_name = #ClassName;                                                   \
                                                                                                               \
                                                                                                               \
        template <typename Visitor>                                                                            \
        static void visit_fields(void *obj, Visitor &&visitor)                                                 \
        {                                                                                                      \
            visit_static_fields([&obj, &visitor](const char *name, auto fieldPtr, auto /*meta*/) {             \
                using ClassType     = _ReflectClass;                                                           \
                using FieldType     = std::decay_t<decltype(std::declval<ClassType &>().*fieldPtr)>;           \
                FieldType &fieldRef = static_cast<ClassType *>(obj)->*fieldPtr;                                \
                std::forward<Visitor>(visitor)(name, fieldRef);                                                \
            });                                                                                                \
        }                                                                                                      \
                                                                                                               \
        reflection_detail()                                                                                    \
        {                                                                                                      \
            ClassRegistry::instance().addPostStaticInitializer([]() {                                          \
                ::Register<_ReflectClass> reg(type_name);                                                      \
                                                                                                               \
                visit_static_fields([&reg](const char *name, auto fieldPtr, auto meta) {                       \
                    reg.property(name, fieldPtr, meta);                                                        \
                    /* TODO: Move into reflects-core*/                                                         \
                    /* 自动注册容器元数据 */                                                                   \
                    using FieldType = std::decay_t<decltype(std::declval<_ReflectClass>().*fieldPtr)>;         \
                    auto &registry  = ClassRegistry::instance();                                               \
                    if (auto *cls = registry.getClass(ya::type_index_v<_ReflectClass>)) {                      \
                        if (auto *prop = cls->getProperty(name)) {                                             \
                            ::ya::reflection::PropertyContainerHelper::tryRegisterContainer<FieldType>(*prop); \
                        }                                                                                      \
                    }                                                                                          \
                });                                                                                            \
                                                                                                               \
                YA_REFLECT_EXTENSION(type_name)                                                                \
            });                                                                                                \
        }                                                                                                      \
                                                                                                               \
        template <typename Visitor>                                                                            \
        static void visit_static_fields(Visitor &&visitor)                                                     \
        {

// YA_REFLECT_FIELD: collect compile-time field list (+ metadata)
#define YA_REFLECT_FIELD(FieldName, ...) \
    std::forward<Visitor>(visitor)(#FieldName, &_ReflectClass::FieldName, ::ya::reflection::MetaBuilder() __VA_ARGS__);

// clang-format off
#define YA_REFLECT_END()                                                                               \
        }                                                                                               \
    };                                                                                                  \
    static inline _reflect_helper_class _reflect_helper_instance{};                                     \
                                                                                                        \
  public:                                                                                               \
    static const char *getTypeName() { return __type_name; }                                            \
    template <typename __ReflectVisitor>                                                                \
    void __visit_properties(__ReflectVisitor &&visitor)                                                 \
    {                                                                                                   \
        reflection_detail::visit_fields(this, std::forward<__ReflectVisitor>(visitor));                 \
    }
// clang-format on



// ============================================================================
// 外部反射 BEGIN/END 宏（用于第三方类型）
// ============================================================================

#define YA_REFLECT_EXTERNAL_BEGIN(ClassName)                                                             \
    namespace ya::reflection::detail                                                                     \
    {                                                                                                    \
    template <>                                                                                          \
    struct ExternalReflect<ClassName>                                                                    \
    {                                                                                                    \
        static constexpr bool has_external_reflection = true;                                            \
        using _ReflectClass                           = ClassName;                                       \
        static constexpr const char *type_name        = #ClassName;                                      \
                                                                                                         \
                                                                                                         \
        template <typename Visitor>                                                                      \
        static void visit_properties(_ReflectClass &__obj, Visitor &&visitor)                            \
        {                                                                                                \
            visit_static_fields([&__obj, &visitor](const char *name, auto fieldPtr, auto /*meta*/) {     \
                using FieldType     = std::decay_t<decltype(std::declval<_ReflectClass &>().*fieldPtr)>; \
                FieldType &fieldRef = __obj.*fieldPtr;                                                   \
                std::forward<Visitor>(visitor)(name, fieldRef);                                          \
            });                                                                                          \
        }                                                                                                \
                                                                                                         \
        struct _reflect_registrar                                                                        \
        {                                                                                                \
            _reflect_registrar()                                                                         \
            {                                                                                            \
                ClassRegistry::instance().addPostStaticInitializer([]() {                                \
                    ::Register<_ReflectClass> reg(type_name);                                            \
                                                                                                         \
                    visit_static_fields([&reg](const char *name, auto fieldPtr, auto meta) {             \
                        reg.property(name, fieldPtr, meta);                                              \
                    });                                                                                  \
                                                                                                         \
                    YA_REFLECT_EXTENSION(type_name)                                                      \
                });                                                                                      \
            }                                                                                            \
        };                                                                                               \
        static inline _reflect_registrar _reflect_helper_instance{};                                     \
                                                                                                         \
        template <typename Visitor>                                                                      \
        static void visit_static_fields(Visitor &&visitor)                                               \
        {

#define YA_REFLECT_EXTERNAL_FIELD(FieldName, ...) YA_REFLECT_FIELD(FieldName, __VA_ARGS__)

// clang-format off
#define YA_REFLECT_EXTERNAL_END()             \
        }                                     \
    };                                        \
    } /* close ya::reflection::detail */ \
    // clang-format on



/*
// blueprint (macro output expectation):

struct AnyStruct
{
  private:
    static inline const char *__type_name = "AnyStruct";
    struct reflection_detail;
    using _reflect_helper_class = reflection_detail;
    struct reflection_detail
    {
        using _ReflectClass                    = AnyStruct;
        static constexpr const char *type_name = "AnyStruct";
        template <typename Visitor>
        static void visit_fields(void *obj, Visitor &&visitor)
        {
            visit_static_fields([&obj, &visitor](const char *name, auto fieldPtr, auto) { using ClassType = _ReflectClass ; using FieldType = std :: decay_t < decltype ( std :: declval < ClassType & > ( ) .* fieldPtr ) > ; FieldType & fieldRef = static_cast < ClassType * > ( obj ) ->* fieldPtr ; std :: forward < Visitor > ( visitor ) ( name , fieldRef ) ; });
        }
        reflection_detail()
        {
            ::Register<_ReflectClass> reg(type_name);
            visit_static_fields([&reg](const char *name, auto fieldPtr, auto meta) { reg.property(name, fieldPtr, meta); });
            ya ::profiling ::StaticInitTimer __static_init_timer___LINE__(type_name);
            ::ya ::reflection ::detail ::registerECSSerializer<_ReflectClass>(type_name);
        }
        template <typename Visitor>
        static void visit_static_fields(Visitor &&visitor)
        {
            std ::forward<Visitor>(visitor)("intValue", &_ReflectClass ::intValue, ::ya ::reflection ::MetaBuilder());
            std ::forward<Visitor>(visitor)("floatValue", &_ReflectClass ::floatValue, ::ya ::reflection ::MetaBuilder());
            std ::forward<Visitor>(visitor)("doubleValue", &_ReflectClass ::doubleValue, ::ya ::reflection ::MetaBuilder());
        }
    };
    static inline _reflect_helper_class _reflect_helper_instance{};

  public:
    static const char *getTypeName() { return __type_name; }
    template <typename __ReflectVisitor>
    void __visit_properties(__ReflectVisitor &&visitor) { reflection_detail ::visit_fields(this, std ::forward<__ReflectVisitor>(visitor)); }

    int    intValue;
    float  floatValue;
    double doubleValue;
};
*/