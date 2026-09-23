#include "Core/Reflection/Reflection.h"
#include <gtest/gtest.h>

// 测试默认构造函数自动注册
struct DefaultConstructorClass
{
    int value = 42;

    YA_REFLECT_BEGIN(DefaultConstructorClass)
    YA_REFLECT_FIELD(value)
    YA_REFLECT_END()
};

// 前向声明
struct CustomConstructorClass;

// 先注册构造函数 trait（必须在类定义之前！）
YA_REGISTER_CONSTRUCTOR(CustomConstructorClass, int, float)

// 测试自定义构造函数注册
struct CustomConstructorClass
{
    int   x;
    float y;

    // 删除默认构造函数
    CustomConstructorClass() = delete;

    // 自定义构造函数
    CustomConstructorClass(int x_val, float y_val) : x(x_val), y(y_val) {}

    YA_REFLECT_BEGIN(CustomConstructorClass)
    YA_REFLECT_FIELD(x)
    YA_REFLECT_FIELD(y)
    YA_REFLECT_END()
};

// TODO: 调试 trait 系统
// static_assert(ya::reflection::detail::has_custom_constructor_v<ya::reflection::detail::RegisterConstructor<CustomConstructorClass>>,
//               "CustomConstructorClass should have custom constructor trait");

// 前向声明
struct MultiConstructorClass;

// 显式注册默认构造函数
YA_REGISTER_CONSTRUCTOR(MultiConstructorClass)

// 测试多个构造函数注册
struct MultiConstructorClass
{
    int         value;
    std::string name;

    MultiConstructorClass() : value(0), name("default") {}
    MultiConstructorClass(int v) : value(v), name("int_ctor") {}
    MultiConstructorClass(int v, const std::string &n) : value(v), name(n) {}

    YA_REFLECT_BEGIN(MultiConstructorClass)
    YA_REFLECT_FIELD(value)
    YA_REFLECT_FIELD(name)
    YA_REFLECT_END()
};

class ConstructorReflectionTest : public ::testing::Test
{
  protected:
    void SetUp() override {}
    void TearDown() override {}
};

// 测试默认构造函数自动注册
TEST_F(ConstructorReflectionTest, DefaultConstructorAutoRegistration)
{
    auto cls = ClassRegistry::instance().getClass(ya::type_index_v<DefaultConstructorClass>);
    ASSERT_NE(cls, nullptr);

    // 检查是否可以创建实例
    EXPECT_TRUE(cls->canCreateInstance()) << "Default constructor should be auto-registered";

    // 尝试创建实例
    void *instance = cls->createInstance();
    ASSERT_NE(instance, nullptr);

    // 验证实例数据
    auto *obj = static_cast<DefaultConstructorClass *>(instance);
    EXPECT_EQ(obj->value, 42);

    // 清理
    cls->destroyInstance(instance);
}

// 测试自定义构造函数注册
TEST_F(ConstructorReflectionTest, CustomConstructorRegistration)
{
    auto cls = ClassRegistry::instance().getClass(ya::type_index_v<CustomConstructorClass>);
    ASSERT_NE(cls, nullptr);

    // 检查是否可以创建实例
    EXPECT_TRUE(cls->canCreateInstance()) << "Custom constructor should be registered";

    // 尝试使用自定义构造函数创建实例
    void *instance = cls->createInstance(123, 45.6f);
    ASSERT_NE(instance, nullptr);

    // 验证实例数据
    auto *obj = static_cast<CustomConstructorClass *>(instance);
    EXPECT_EQ(obj->x, 123);
    EXPECT_FLOAT_EQ(obj->y, 45.6f);

    // 清理
    cls->destroyInstance(instance);
}

// 测试显式默认构造函数注册
TEST_F(ConstructorReflectionTest, ExplicitDefaultConstructorRegistration)
{
    auto cls = ClassRegistry::instance().getClass(ya::type_index_v<MultiConstructorClass>);
    ASSERT_NE(cls, nullptr);

    // 检查是否可以创建实例
    EXPECT_TRUE(cls->canCreateInstance()) << "Explicit default constructor should be registered";

    // 尝试创建实例
    void *instance = cls->createInstance();
    ASSERT_NE(instance, nullptr);

    // 验证实例数据
    auto *obj = static_cast<MultiConstructorClass *>(instance);
    EXPECT_EQ(obj->value, 0);
    EXPECT_EQ(obj->name, "default");

    // 清理
    cls->destroyInstance(instance);
}