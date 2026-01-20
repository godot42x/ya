#include "Core/Reflection/Reflection.h"
#include <gtest/gtest.h>

// 简单测试类
struct SimpleClass
{
    int value = 42;
    
    YA_REFLECT_BEGIN(SimpleClass)
    YA_REFLECT_FIELD(value)
    YA_REFLECT_END()
};

// 前向声明
struct CustomClass;

// 先注册自定义构造函数（必须在类定义之前！）
YA_REGISTER_CONSTRUCTOR(CustomClass, int, float)

// 测试自定义构造函数的类
struct CustomClass
{
    int x;
    float y;
    
    CustomClass() = delete;
    CustomClass(int x_val, float y_val) : x(x_val), y(y_val) {}
    
    YA_REFLECT_BEGIN(CustomClass)
    YA_REFLECT_FIELD(x)
    YA_REFLECT_FIELD(y)
    YA_REFLECT_END()
};

class SimpleConstructorTest : public ::testing::Test
{
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(SimpleConstructorTest, DefaultConstructor)
{
    auto cls = ClassRegistry::instance().getClass(ya::type_index_v<SimpleClass>);
    ASSERT_NE(cls, nullptr);
    EXPECT_TRUE(cls->canCreateInstance());
    
    void* instance = cls->createInstance();
    ASSERT_NE(instance, nullptr);
    
    auto* obj = static_cast<SimpleClass*>(instance);
    EXPECT_EQ(obj->value, 42);
    
    cls->destroyInstance(instance);
}

TEST_F(SimpleConstructorTest, CustomConstructor)
{
    auto cls = ClassRegistry::instance().getClass(ya::type_index_v<CustomClass>);
    ASSERT_NE(cls, nullptr);
    EXPECT_TRUE(cls->canCreateInstance());
    
    void* instance = cls->createInstance(123, 45.6f);
    ASSERT_NE(instance, nullptr);
    
    auto* obj = static_cast<CustomClass*>(instance);
    EXPECT_EQ(obj->x, 123);
    EXPECT_FLOAT_EQ(obj->y, 45.6f);
    
    cls->destroyInstance(instance);
}