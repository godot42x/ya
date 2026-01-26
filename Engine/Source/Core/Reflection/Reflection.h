#pragma once

#include "Core/Common/FWD-std.h"



#include "Core/Profiling/StaticInitProfiler.h"

#include "Core/Reflection/MetadataSupport.h"

#include "Core/Macro/VariadicMacros.h"
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

// Constructor registration traits
template <typename T, typename... Args>
struct RegisterConstructorBase
{
    using type                            = T;
    using construct_args                  = refl::type_list<Args...>;
    static constexpr bool has_custom_ctor = true;
};

// Default trait - no custom constructor
template <typename T>
struct RegisterConstructor
{
    // Empty - indicates no custom constructor
    static constexpr bool has_custom_ctor = false;
};

// Type trait to check if a type has custom constructor registration
// Check if RegisterConstructor<T> has been specialized to inherit from RegisterConstructorBase
template <typename T>
inline constexpr bool has_custom_constructor_v = RegisterConstructor<T>::has_custom_ctor;

} // namespace ya::reflection::detail

// Convenience namespace alias
namespace ya::reflection
{
using detail::has_custom_constructor_v;
using detail::RegisterConstructor;
using detail::RegisterConstructorBase;
} // namespace ya::reflection

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

inline type_index_t getRuntimeObjectType(const void *obj)
{
    // return ::ya::ECSRegistry::get().getRuntimeObjectType(obj);
}


// inline void make_pretty_type_name(std::string_view &name)
// {
//     printf("name: %s\n", std::string(name).c_str());
//     if (name.starts_with("struct")) {
//         name.remove_prefix(6);
//     }
//     if (name.starts_with("class")) {
//         name.remove_prefix(5);
//     }
//     // trim leading spaces
//     while (*name.begin() == ' ') {
//         name.remove_prefix(1);
//     }
//     while (*name.rbegin() == ' ') {
//         name.remove_suffix(1);
//     }
//     printf("name': %s\n", std::string(name).c_str());
// }

} // namespace reflection


} // namespace ya



// Notes:
// - Invoked inside a scope where `class_t` and `type_name` exist.
// - Keep extensions (profiling, ECS serializer, custom registry hooks) here.
#ifndef YA_REFLECT_EXTENSION
    #define ___YA_REFLECT_EXTENSION(type_name) \
        ::ya::ECSRegistry::get().registerComponent<type_name>(#type_name);
#endif

// #define ___YA_CREATE_REGISTER(ClassName)                   \
//     std::string_view real_name = typeid(ClassName).name(); \
//     ::ya::reflection::make_pretty_type_name(real_name);    \
//     auto name = std::string(real_name);                    \
//     YA_PROFILE_STATIC_INIT(name.c_str());                  \
//     std::unique_ptr<::Register<class_t>> reg = std::make_unique<::Register<class_t>>(name);

#define ___YA_CREATE_REGISTER_2(ClassName)                         \
    auto real_name = typeid(ClassName).name();                     \
    YA_PROFILE_STATIC_INIT(real_name);                             \
                                                                   \
    constexpr std::string_view           macroName = #ClassName;   \
    std::unique_ptr<::Register<class_t>> reg       = nullptr;      \
    if constexpr (macroName.find('<') != std::string_view::npos) { \
        reg = std::make_unique<::Register<class_t>>(real_name);    \
    }                                                              \
    else {                                                         \
        reg = std::make_unique<::Register<class_t>>(#ClassName);   \
    }


#define ___YA_REFLECT_BEGIN_IMPL(ClassName, BaseClass)                                                                                            \
  private:                                                                                                                                        \
    struct reflection_detail;                                                                                                                     \
    using _reflect_helper_class = reflection_detail; /* const std::string ge*/                                                                    \
    struct reflection_detail                                                                                                                      \
    {                                                                                                                                             \
        using class_t                        = ClassName;                                                                                         \
        using base_t                         = BaseClass;                                                                                         \
        static constexpr bool has_base_class = !std::is_same_v<base_t, void>;                                                                     \
                                                                                                                                                  \
        reflection_detail()                                                                                                                       \
        {                                                                                                                                         \
            ClassRegistry::instance().addPostStaticInitializer([]() { reflection_detail::real_init(); });                                         \
        }                                                                                                                                         \
        static void real_init()                                                                                                                   \
        {                                                                                                                                         \
            ___YA_CREATE_REGISTER_2(ClassName)                                                                                                    \
                                                                                                                                                  \
            /* Register base class relationship if applicable */                                                                                  \
            if constexpr (has_base_class) {                                                                                                       \
                reg->template parentClass<base_t>();                                                                                              \
            }                                                                                                                                                  \
            visit_static_fields([&reg](const char *name, auto fieldPtr, auto meta) {                                                              \
                reg->property(name, fieldPtr, meta);                                                                                              \
                                                                                                                                                  \
                /* TODO: Move into reflects-core*/                                                                                                \
                using FieldType = std::decay_t<decltype(std::declval<class_t>().*fieldPtr)>;                                                      \
                auto &registry  = ClassRegistry::instance();                                                                                      \
                if (auto *cls = registry.getClass(ya::type_index_v<class_t>)) {                                                                   \
                    if (auto *prop = cls->getProperty(name)) {                                                                                    \
                        ::ya::reflection::PropertyContainerHelper::tryRegisterContainer<FieldType>(*prop);                                        \
                    }                                                                                                                             \
                }                                                                                                                                 \
            });                                                                                                                                   \
                                                                                                                                                  \
            /* Register constructors */                                                                                                           \
            register_constructors(*reg);                                                                                                          \
                                                                                                                                                  \
            ___YA_REFLECT_EXTENSION(ClassName)                                                                                                    \
        }                                                                                                                                         \
        template <typename Visitor>                                                                                                               \
        static void visit_fields(void *obj, Visitor &&visitor)                                                                                    \
        {                                                                                                                                         \
            visit_static_fields([&obj, &visitor](const char *name, auto fieldPtr, auto /*meta*/) {                                                \
                using ClassType     = class_t;                                                                                                    \
                using FieldType     = std::decay_t<decltype(std::declval<ClassType &>().*fieldPtr)>;                                              \
                FieldType &fieldRef = static_cast<ClassType *>(obj)->*fieldPtr;                                                                   \
                std::forward<Visitor>(visitor)(name, fieldRef);                                                                                   \
            });                                                                                                                                   \
        }                                                                                                                                         \
                                                                                                                                                  \
        /* Constructor registration using trait-based approach */                                                                                 \
        template <typename RegType>                                                                                                               \
        static void register_constructors(RegType &reg)                                                                                           \
        {                                                                                                                                         \
            register_constructors_impl(reg);                                                                                                      \
        }                                                                                                                                         \
                                                                                                                                                  \
        template <typename RegType>                                                                                                               \
        static void register_constructors_impl(RegType &reg)                                                                                      \
        {                                                                                                                                         \
            using ConstructorTrait = ::ya::reflection::detail::RegisterConstructor<class_t>;                                                      \
            if constexpr (ConstructorTrait::has_custom_ctor) {                                                                                    \
                /* Use custom constructor from trait in your code for extension */                                                                \
                register_constructor_from_trait(reg, ConstructorTrait{});                                                                         \
            }                                                                                                                                     \
            /* register default constructor if could */                                                                                           \
            if constexpr (std::is_default_constructible_v<class_t>) {                                                                             \
                reg.template constructor<>();                                                                                                     \
            }                                                                                                                                     \
        }                                                                                                                                         \
                                                                                                                                                  \
        template <typename RegType, typename ConstructClassType, typename... Args>                                                                \
        static void register_constructor_from_trait(RegType &reg, ::ya::reflection::detail::RegisterConstructorBase<ConstructClassType, Args...>) \
        {                                                                                                                                         \
            reg.template constructor<Args...>();                                                                                                  \
        }                                                                                                                                         \
        template <typename Visitor>                                                                                                               \
        static void visit_static_fields(Visitor &&visitor)                                                                                        \
        {

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

// Macro for easy constructor registration
// Usage: YA_REGISTER_CONSTRUCTOR(MyClass, int, float)
//        YA_REGISTER_CONSTRUCTOR(MyClass)  // for default constructor (explicit)
#define YA_REGISTER_CONSTRUCTOR(ClassName, ...)                                             \
    namespace ya::reflection::detail                                                        \
    {                                                                                       \
    template <>                                                                             \
    struct RegisterConstructor<ClassName> : RegisterConstructorBase<ClassName, __VA_ARGS__> \
    {};                                                                                     \
    }

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
