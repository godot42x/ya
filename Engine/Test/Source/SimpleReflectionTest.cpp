// 简单测试新反射系统
#include "Core/Reflection/Reflection.h"
#include <gtest/gtest.h>
#include <cassert>


struct TestComponent
{
    int         value = 42;
    std::string name  = "test";

    YA_REFLECT_BEGIN(TestComponent)
    YA_REFLECT_FIELD(value, )
    YA_REFLECT_FIELD(name, )
    YA_REFLECT_END()
};

class SimpleReflectionTest : public ::testing::Test
{
  protected:
    void SetUp() override {}
    void TearDown() override {}
};

// 测试1: 类型名称
TEST_F(SimpleReflectionTest, TypeName)
{
    auto cls = ClassRegistry::instance().getClass(ya::type_index_v<TestComponent>);
    ASSERT_NE(cls, nullptr);
    EXPECT_EQ(std::string(cls->getName()), "TestComponent");
}

// 测试2: 属性遍历
TEST_F(SimpleReflectionTest, PropertyIteration)
{
    TestComponent p;
    int propCount = 0;
    p.__visit_properties([&](const char *name, auto &value) {
        propCount++;
    });

    EXPECT_EQ(propCount, 2); // value, name
}
