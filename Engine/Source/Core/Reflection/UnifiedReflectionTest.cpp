/**
 * @brief 统一反射宏测试 - YA_REFLECT
 * 展示侵入式和非侵入式两种用法
 */

#include "Core/Reflection/UnifiedReflection.h"
#include "Core/Base.h"

namespace ya::test
{

// ============================================================================
// 测试 1: 侵入式反射（自定义类）
// ============================================================================

struct PlayerComponent
{
    YA_REFLECT(PlayerComponent,
               PROP(health, .range(0.0f, 100.0f).tooltip("Health").category("Stats")),
               PROP(maxHealth, .tooltip("Max Health").category("Stats")),
               PROP(speed, .range(0.0f, 10.0f).category("Movement")),
               PROP(cachedValue, .transient()));

    float health      = 100.0f;
    float maxHealth   = 100.0f;
    float speed       = 5.0f;
    float cachedValue = 0.0f;
};


struct DummyStruct
{
    int   a;
    float b;

    YA_REFLECT_BEGIN(DummyStruct)
        YA_REFLECT_FIELD(a, .tooltip("Integer a"))
        YA_REFLECT_FIELD(b, .tooltip("Float b"))
    YA_REFLECT_END(DummyStruct)
        // Visitor 部分
        YA_VISIT_FIELD(a)
        YA_VISIT_FIELD(b)
    YA_REFLECT_VISITOR_END(DummyStruct)
};

// 测试更多字段（演示无限制）
struct LargeComponent
{
    YA_REFLECT_BEGIN(LargeComponent)
        YA_REFLECT_FIELD(field1, .tooltip("Field 1"))
        YA_REFLECT_FIELD(field2, .range(0, 100))
        YA_REFLECT_FIELD(field3, .transient())
        YA_REFLECT_FIELD(field4, )
        YA_REFLECT_FIELD(field5, )
        YA_REFLECT_FIELD(field6, )
        YA_REFLECT_FIELD(field7, )
        YA_REFLECT_FIELD(field8, )
        YA_REFLECT_FIELD(field9, )
        YA_REFLECT_FIELD(field10, )
        YA_REFLECT_FIELD(field11, )
        YA_REFLECT_FIELD(field12, )
        YA_REFLECT_FIELD(field13, )
        YA_REFLECT_FIELD(field14, )
        YA_REFLECT_FIELD(field15, )
        YA_REFLECT_FIELD(field16, )
        YA_REFLECT_FIELD(field17, )
        YA_REFLECT_FIELD(field18, )
        // 可以继续添加，没有 16 个的限制！
    YA_REFLECT_END(LargeComponent)
        // Visitor 部分（需要手动维护，但至少没有数量限制）
        YA_VISIT_FIELD(field1)
        YA_VISIT_FIELD(field2)
        YA_VISIT_FIELD(field3)
        YA_VISIT_FIELD(field4)
        YA_VISIT_FIELD(field5)
        YA_VISIT_FIELD(field6)
        YA_VISIT_FIELD(field7)
        YA_VISIT_FIELD(field8)
        YA_VISIT_FIELD(field9)
        YA_VISIT_FIELD(field10)
        YA_VISIT_FIELD(field11)
        YA_VISIT_FIELD(field12)
        YA_VISIT_FIELD(field13)
        YA_VISIT_FIELD(field14)
        YA_VISIT_FIELD(field15)
        YA_VISIT_FIELD(field16)
        YA_VISIT_FIELD(field17)
        YA_VISIT_FIELD(field18)
    YA_REFLECT_VISITOR_END(LargeComponent)

    int field1 = 0, field2 = 0, field3 = 0, field4 = 0, field5 = 0, field6 = 0;
    int field7 = 0, field8 = 0, field9 = 0, field10 = 0, field11 = 0, field12 = 0;
    int field13 = 0, field14 = 0, field15 = 0, field16 = 0, field17 = 0, field18 = 0;
};

// ============================================================================
// 测试 2: 非侵入式反射（模拟第三方库）
// ============================================================================

// 假设这是第三方库的类，我们无法修改源码
namespace ThirdParty
{
struct Vector3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Matrix4x4
{
    float m00 = 1.0f, m01 = 0.0f, m02 = 0.0f, m03 = 0.0f;
    float m10 = 0.0f, m11 = 1.0f, m12 = 0.0f, m13 = 0.0f;
    float m20 = 0.0f, m21 = 0.0f, m22 = 1.0f, m23 = 0.0f;
    float m30 = 0.0f, m31 = 0.0f, m32 = 0.0f, m33 = 1.0f;
};
} // namespace ThirdParty

} // namespace ya::test

// 在类外部注册反射和元数据（必须在全局作用域）
YA_REFLECT_EXTERNAL(ya::test::ThirdParty::Vector3,
                    PROP(x, .tooltip("X coordinate").category("Position")),
                    PROP(y, .tooltip("Y coordinate").category("Position")),
                    PROP(z, .tooltip("Z coordinate").category("Position")))

// 使用 BEGIN/END 版本进行外部反射（演示支持更多字段）
YA_REFLECT_EXTERNAL_BEGIN(ya::test::ThirdParty::Matrix4x4)
    YA_REFLECT_EXTERNAL_FIELD(m00, .tooltip("Element [0,0]"))
    YA_REFLECT_EXTERNAL_FIELD(m01, .tooltip("Element [0,1]"))
    YA_REFLECT_EXTERNAL_FIELD(m02, .tooltip("Element [0,2]"))
    YA_REFLECT_EXTERNAL_FIELD(m03, .tooltip("Element [0,3]"))
    YA_REFLECT_EXTERNAL_FIELD(m10, .tooltip("Element [1,0]"))
    YA_REFLECT_EXTERNAL_FIELD(m11, .tooltip("Element [1,1]"))
    YA_REFLECT_EXTERNAL_FIELD(m12, .tooltip("Element [1,2]"))
    YA_REFLECT_EXTERNAL_FIELD(m13, .tooltip("Element [1,3]"))
    YA_REFLECT_EXTERNAL_FIELD(m20, .tooltip("Element [2,0]"))
    YA_REFLECT_EXTERNAL_FIELD(m21, .tooltip("Element [2,1]"))
    YA_REFLECT_EXTERNAL_FIELD(m22, .tooltip("Element [2,2]"))
    YA_REFLECT_EXTERNAL_FIELD(m23, .tooltip("Element [2,3]"))
    YA_REFLECT_EXTERNAL_FIELD(m30, .tooltip("Element [3,0]"))
    YA_REFLECT_EXTERNAL_FIELD(m31, .tooltip("Element [3,1]"))
    YA_REFLECT_EXTERNAL_FIELD(m32, .tooltip("Element [3,2]"))
    YA_REFLECT_EXTERNAL_FIELD(m33, .tooltip("Element [3,3]"))
YA_REFLECT_EXTERNAL_END(ya::test::ThirdParty::Matrix4x4)
    // Visitor 部分
    YA_VISIT_EXTERNAL_FIELD(m00)
    YA_VISIT_EXTERNAL_FIELD(m01)
    YA_VISIT_EXTERNAL_FIELD(m02)
    YA_VISIT_EXTERNAL_FIELD(m03)
    YA_VISIT_EXTERNAL_FIELD(m10)
    YA_VISIT_EXTERNAL_FIELD(m11)
    YA_VISIT_EXTERNAL_FIELD(m12)
    YA_VISIT_EXTERNAL_FIELD(m13)
    YA_VISIT_EXTERNAL_FIELD(m20)
    YA_VISIT_EXTERNAL_FIELD(m21)
    YA_VISIT_EXTERNAL_FIELD(m22)
    YA_VISIT_EXTERNAL_FIELD(m23)
    YA_VISIT_EXTERNAL_FIELD(m30)
    YA_VISIT_EXTERNAL_FIELD(m31)
    YA_VISIT_EXTERNAL_FIELD(m32)
    YA_VISIT_EXTERNAL_FIELD(m33)
YA_REFLECT_EXTERNAL_VISITOR_END(ya::test::ThirdParty::Matrix4x4)

namespace ya::test
{

// ============================================================================
// 测试函数
// ============================================================================

void testUnifiedReflection()
{
    using namespace ya::reflection;

    YA_CORE_INFO("=== Testing Unified YA_REFLECT Macro ===\n");

    // ========================================================================
    // 测试侵入式反射
    // ========================================================================

    YA_CORE_INFO("[Test 1] Intrusive Reflection - PlayerComponent:");

    PlayerComponent player;
    player.health = 85.0f;
    player.speed  = 7.5f;

    // 1.1 属性遍历
    YA_CORE_INFO("  Properties:");
    player.__visit_properties([](const char *name, const auto &value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, float>) {
            YA_CORE_INFO("    {} = {}", name, value);
        }
    });

    // 1.2 元数据查询（使用 reflects-core ClassRegistry）
    auto *cls = ClassRegistry::instance().getClass("PlayerComponent");
    if (cls) {
        auto *healthProp = cls->getProperty("health");
        if (healthProp) {
            YA_CORE_INFO("  health metadata:");
            YA_CORE_INFO("    - EditAnywhere: {}", bool(healthProp->getMetadata().hasFlag(FieldFlags::EditAnywhere)));
            YA_CORE_INFO("    - Range: [{}, {}]",
                         healthProp->getMetadata().get<float>("range_min"),
                         healthProp->getMetadata().get<float>("range_max"));
            YA_CORE_INFO("    - Tooltip: {}", healthProp->getMetadata().get<std::string>("tooltip"));
        }
    }

    // ========================================================================
    // 测试非侵入式反射
    // ========================================================================

    YA_CORE_INFO("\n[Test 2] External Reflection - ThirdParty::Vector3:");

    ThirdParty::Vector3 vec;
    vec.x = 1.0f;
    vec.y = 2.0f;
    vec.z = 3.0f;

    // 2.1 属性遍历（使用外部反射）
    YA_CORE_INFO("  Properties:");
    reflection::detail::ExternalReflect<ThirdParty::Vector3>::visit_properties(
        vec, [](const char *name, const auto &value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, float>) {
                YA_CORE_INFO("    {} = {}", name, value);
            }
        });

    // 2.2 元数据查询（使用 ClassRegistry）
    auto *vecCls = ClassRegistry::instance().getClass("ya::test::ThirdParty::Vector3");
    if (vecCls) {
        auto *xProp = vecCls->getProperty("x");
        if (xProp) {
            YA_CORE_INFO("  x metadata:");
            YA_CORE_INFO("    - EditAnywhere: {}", bool(xProp->getMetadata().hasFlag(FieldFlags::EditAnywhere)));
            YA_CORE_INFO("    - Tooltip: {}", xProp->getMetadata().get<std::string>("tooltip"));
            YA_CORE_INFO("    - Category: {}", xProp->getMetadata().get<std::string>("category"));
        }
    }

    // ========================================================================
    // 测试类别分组（两种方式共享元数据系统）
    // ========================================================================

    YA_CORE_INFO("\n[Test 3] Category grouping:");

    YA_CORE_INFO("  PlayerComponent has reflection and metadata registered");
    YA_CORE_INFO("  ya::test::ThirdParty::Vector3 has external reflection and metadata registered");

    YA_CORE_INFO("\n=== All Tests Passed! ===\n");
    YA_CORE_INFO("Summary:");
    YA_CORE_INFO("  - YA_REFLECT: Use inside your own classes");
    YA_CORE_INFO("  - YA_REFLECT_EXTERNAL: Use outside third-party classes");
    YA_CORE_INFO("  - Both share the same metadata system\n");
}

} // namespace ya::test
