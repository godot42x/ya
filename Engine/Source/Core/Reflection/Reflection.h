#pragma once

#include "Core/Common/FWD-std.h"



#include "Core/Profiling/StaticInitProfiler.h"

#include "Core/Reflection/MetadataSupport.h"

#include "Core/Reflection/PropertyExtensions.h"


// TODO: should not be in core?
#include "Core/Reflection/ECSRegistry.h"

namespace ya
{
struct IComponent;
}


namespace ya::reflection::detail
{
// 前向声明 ExternalReflect
template <typename T>
struct ExternalReflect;
} // namespace ya::reflection::detail

namespace ya
{

namespace reflection
{

namespace detail
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

} // namespace detail



template <typename T>
struct Visitor
{
    static void visit_properties(T &obj, auto &&visitor)
    {
        if constexpr (reflection::detail::has_external_reflect_v<T>) {
            ::ya::reflection::detail::ExternalReflect<T>::visit_properties(obj, std::forward<decltype(visitor)>(visitor));
        }
        else {
            obj.__visit_properties(std::forward<decltype(visitor)>(visitor));
        }
    }
};

template <>
struct Visitor<void>
{
    static void visit_properties(auto &obj, auto &&visitor) {}
};


} // namespace reflection


} // namespace ya



// Notes:
// - Invoked inside a scope where `class_t` and `type_name` exist.
// - Keep extensions (profiling, ECS serializer, custom registry hooks) here.
#ifndef YA_REFLECT_EXTENSION
    #define ___YA_REFLECT_EXTENSION(type_name) \
        ::ya::ECSRegistry::get().registerComponent<type_name>(#type_name);
#endif


#define ___YA_REFLECT_BEGIN_IMPL(ClassName, BaseClass)                                                         \
  private:                                                                                                     \
    struct reflection_detail;                                                                                  \
    using _reflect_helper_class = reflection_detail;                                                           \
    struct reflection_detail                                                                                   \
    {                                                                                                          \
        using class_t                        = ClassName;                                                      \
        using base_t                         = BaseClass;                                                      \
        static constexpr bool has_base_class = !std::is_same_v<base_t, void>;                                  \
                                                                                                               \
        reflection_detail()                                                                                    \
        {                                                                                                      \
            ClassRegistry::instance().addPostStaticInitializer([]() {                                          \
                YA_PROFILE_STATIC_INIT(#ClassName);                                                            \
                ::Register<class_t> reg(#ClassName);                                                           \
                if constexpr (has_base_class) {                                                                \
                    reg.parentClass<base_t>();                                                                 \
                }                                                                                              \
                                                                                                               \
                visit_static_fields([&reg](const char *name, auto fieldPtr, auto meta) {                       \
                    reg.property(name, fieldPtr, meta);                                                        \
                                                                                                               \
                    /* TODO: Move into reflects-core*/                                                         \
                    using FieldType = std::decay_t<decltype(std::declval<class_t>().*fieldPtr)>;               \
                    auto &registry  = ClassRegistry::instance();                                               \
                    if (auto *cls = registry.getClass(ya::type_index_v<class_t>)) {                            \
                        if (auto *prop = cls->getProperty(name)) {                                             \
                            ::ya::reflection::PropertyContainerHelper::tryRegisterContainer<FieldType>(*prop); \
                        }                                                                                      \
                    }                                                                                          \
                });                                                                                            \
                ___YA_REFLECT_EXTENSION(ClassName)                                                             \
            });                                                                                                \
        }                                                                                                      \
                                                                                                               \
        template <typename Visitor>                                                                            \
        static void visit_fields(void *obj, Visitor &&visitor)                                                 \
        {                                                                                                      \
            visit_static_fields([&obj, &visitor](const char *name, auto fieldPtr, auto /*meta*/) {             \
                using ClassType     = class_t;                                                                 \
                using FieldType     = std::decay_t<decltype(std::declval<ClassType &>().*fieldPtr)>;           \
                FieldType &fieldRef = static_cast<ClassType *>(obj)->*fieldPtr;                                \
                std::forward<Visitor>(visitor)(name, fieldRef);                                                \
            });                                                                                                \
        }                                                                                                      \
                                                                                                               \
        template <typename Visitor>                                                                            \
        static void visit_static_fields(Visitor &&visitor)                                                     \
        {

#include "Core/Macro/VariadicMacros.h"

// 支持 1 个参数（无父类）或 2 个参数（有父类）
#define YA_REFLECT_BEGIN(...) \
    YA_EXPAND(YA_CALL_MACRO_N(___YA_REFLECT_BEGIN_, __VA_ARGS__)(__VA_ARGS__))

#define ___YA_REFLECT_BEGIN_1(ClassName) \
    ___YA_REFLECT_BEGIN_IMPL(ClassName, void)

#define ___YA_REFLECT_BEGIN_2(ClassName, BaseClass) \
    ___YA_REFLECT_BEGIN_IMPL(ClassName, BaseClass)

// YA_REFLECT_FIELD: collect compile-time field list (+ metadata)
#define YA_REFLECT_FIELD(FieldName, ...) \
    std::forward<Visitor>(visitor)(#FieldName, &class_t::FieldName, ::ya::reflection::MetaBuilder<decltype(class_t::FieldName)>() __VA_ARGS__);

// clang-format off
#define YA_REFLECT_END()                                                                               \
        }                                                                                               \
    };                                                                                                  \
    static inline _reflect_helper_class _reflect_helper_instance{};                                     \
                                                                                                        \
  public:                                                                                               \
    template <typename __ReflectVisitor>                                                                \
    void __visit_properties(__ReflectVisitor &&visitor)                                                 \
    {                                                                                                   \
        reflection_detail::visit_fields(this, std::forward<__ReflectVisitor>(visitor));                 \
    }
// clang-format on



// ============================================================================
// 外部反射 BEGIN/END 宏（用于第三方类型）
// ============================================================================
#define YA_REFLECT_EXTERNAL_FIELD(FieldName, ...) YA_REFLECT_FIELD(FieldName, __VA_ARGS__)


// ============================================================================
// 枚举反射宏 (YA_REFLECT_ENUM_*)
// ============================================================================

/**
 * @brief 开始枚举反射定义
 * @param EnumType 枚举类型（支持 enum class 和普通 enum）
 *
 * 用法:
 * YA_REFLECT_ENUM_BEGIN(ya::EPrimitiveGeometry)
 *     YA_REFLECT_ENUM_VALUE(None)
 *     YA_REFLECT_ENUM_VALUE(Cube)
 *     YA_REFLECT_ENUM_VALUE(Sphere)
 * YA_REFLECT_ENUM_END()
 *
 * NOTE: Uses delayed initialization via addPostStaticInitializer to ensure
 *       type_index_v<EnumType> is properly initialized (not 0).
 */
#define YA_REFLECT_ENUM_BEGIN(EnumType)       \
    namespace                                 \
    {                                         \
    struct _YA_ENUM_REFLECT_STRUCT_##__LINE__ \
    {                                         \
        using _EnumType = EnumType;           \
        _YA_ENUM_REFLECT_STRUCT_##__LINE__()  \
        {                                     \
            ClassRegistry::instance().addPostStaticInitializer([]() {                   \
                RegisterEnum<_EnumType> reg(#EnumType, ::ya::type_index_v<_EnumType>);  \
                reg

/**
 * @brief 注册枚举值（自动使用枚举成员名称）
 * @param ValueName 枚举值名称（不需要带枚举前缀）
 */
#define YA_REFLECT_ENUM_VALUE(ValueName) \
    .value(#ValueName, _EnumType::ValueName)

/**
 * @brief 注册枚举值（自定义显示名称）
 * @param ValueName 枚举值名称
 * @param DisplayName 自定义显示名称字符串
 */
#define YA_REFLECT_ENUM_VALUE_NAMED(ValueName, DisplayName) \
    .value(DisplayName, _EnumType::ValueName)

/**
 * @brief 结束枚举反射定义
 */
// clang-format off
#define YA_REFLECT_ENUM_END()                                                           \
                ;                                                                       \
            });                                                                         \
        }                                                                               \
    };                                                                                  \
    static _YA_ENUM_REFLECT_STRUCT_##__LINE__ _g_ya_enum_reflect_instance_##__LINE__;   \
    }
  // clang-format on



#define YA_REFLECT_BEGIN_EXTERNAL(ClassName)                  \
    namespace ya::reflection::detail                          \
    {                                                         \
    template <>                                               \
    struct ExternalReflect<ClassName>                         \
    {                                                         \
        static constexpr bool has_external_reflection = true; \
        ___YA_REFLECT_BEGIN_IMPL(ClassName, void)


// clang-format off
#define YA_REFLECT_END_EXTERNAL()             \
        YA_REFLECT_END()                      \
    };                                        \
    } /* close ya::reflection::detail */
// clang-format on
