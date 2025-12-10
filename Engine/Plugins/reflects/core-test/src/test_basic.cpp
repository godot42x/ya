#include "test_common.h"

// ============================================================================
// Basic Reflection Tests - 函数调用和属性访问
// ============================================================================

class BasicReflectionTest : public ::testing::Test
{
  protected:
    Class *personClass;
    Person testPerson;

    void SetUp() override
    {
        personClass = ClassRegistry::instance().getClass("Person");
        ASSERT_NE(personClass, nullptr);

        testPerson.name = "Alice";
        testPerson.age  = 30;
    }
};

// ============================================================================
// Function Invocation Tests
// ============================================================================

TEST_F(BasicReflectionTest, CallMemberFunction)
{
    // 调用成员函数
    int result = personClass->call<int>("addToAge", &testPerson, 5);
    EXPECT_EQ(result, 35); // 30 + 5
}

TEST_F(BasicReflectionTest, CallVoidFunction)
{
    // 调用void返回值的函数
    personClass->call<void>("setInfo", &testPerson, std::string("Bob"), 25);

    EXPECT_EQ(testPerson.name, "Bob");
    EXPECT_EQ(testPerson.age, 25);
}

TEST_F(BasicReflectionTest, CallConstMemberFunction)
{
    // 调用const成员函数
    std::string name = personClass->call<std::string>("getName", &testPerson);
    int         age  = personClass->call<int>("getAge", &testPerson);

    EXPECT_EQ(name, "Alice");
    EXPECT_EQ(age, 30);
}

TEST_F(BasicReflectionTest, CallStaticFunction)
{
    // 调用静态函数
    int product = personClass->callStatic<int>("multiply", 6, 7);
    EXPECT_EQ(product, 42);
}

TEST_F(BasicReflectionTest, InvokeWithArgumentList)
{
    // 使用ArgumentList调用
    auto args   = ArgumentList::make(10);
    auto result = personClass->invoke("addToAge", &testPerson, args);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::any_cast<int>(result), 40); // 30 + 10
}

// ============================================================================
// Property Access Tests
// ============================================================================

TEST_F(BasicReflectionTest, GetPropertyValue)
{
    std::string name = personClass->getPropertyValue<std::string>("name", &testPerson);
    int         age  = personClass->getPropertyValue<int>("age", &testPerson);

    EXPECT_EQ(name, "Alice");
    EXPECT_EQ(age, 30);
}

TEST_F(BasicReflectionTest, SetPropertyValue)
{
    personClass->setPropertyValue("name", &testPerson, std::string("Charlie"));
    personClass->setPropertyValue("age", &testPerson, 35);

    EXPECT_EQ(testPerson.name, "Charlie");
    EXPECT_EQ(testPerson.age, 35);
}

TEST_F(BasicReflectionTest, PropertyRoundTrip)
{
    // 通过反射设置
    personClass->setPropertyValue("name", &testPerson, std::string("David"));
    personClass->setPropertyValue("age", &testPerson, 40);

    // 通过反射读取
    std::string name = personClass->getPropertyValue<std::string>("name", &testPerson);
    int         age  = personClass->getPropertyValue<int>("age", &testPerson);

    // 验证
    EXPECT_EQ(name, "David");
    EXPECT_EQ(age, 40);
    EXPECT_EQ(testPerson.name, "David");
    EXPECT_EQ(testPerson.age, 40);
}

// ============================================================================
// Introspection Tests
// ============================================================================

TEST_F(BasicReflectionTest, CheckFunctionExists)
{
    EXPECT_TRUE(personClass->hasFunction("addToAge"));
    EXPECT_TRUE(personClass->hasFunction("getName"));
    EXPECT_TRUE(personClass->hasFunction("multiply"));
    EXPECT_FALSE(personClass->hasFunction("nonexistent"));
}

TEST_F(BasicReflectionTest, CheckPropertyExists)
{
    EXPECT_TRUE(personClass->hasProperty("name"));
    EXPECT_TRUE(personClass->hasProperty("age"));
    EXPECT_FALSE(personClass->hasProperty("nonexistent"));
}

TEST_F(BasicReflectionTest, GetFunctionInfo)
{
    const Function *func = personClass->getFunction("addToAge");
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->name, "addToAge");
    EXPECT_EQ(func->argCount, 1);
    EXPECT_FALSE(func->isStatic());

    const Function *staticFunc = personClass->getFunction("multiply");
    ASSERT_NE(staticFunc, nullptr);
    EXPECT_TRUE(staticFunc->isStatic());
}

TEST_F(BasicReflectionTest, GetPropertyInfo)
{
    const Property *prop = personClass->getProperty("name");
    ASSERT_NE(prop, nullptr);
    EXPECT_EQ(prop->name, "name");
    EXPECT_EQ(prop->isType<std::string>(), true);
    EXPECT_FALSE(prop->bConst);
    EXPECT_FALSE(prop->bStatic);
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST_F(BasicReflectionTest, ErrorOnNonexistentFunction)
{
    EXPECT_THROW(
        personClass->call<int>("nonexistent", &testPerson, 42),
        std::runtime_error);
}

TEST_F(BasicReflectionTest, ErrorOnNonexistentProperty)
{
    EXPECT_THROW(
        personClass->getPropertyValue<int>("nonexistent", &testPerson),
        std::runtime_error);
}

// TEST_F(BasicReflectionTest, ErrorOnNullPointer)
// {
//     EXPECT_THROW(
//         personClass->call<int>("addToAge", nullptr, 5),
//         std::runtime_error
//     );
// }


TEST_F(BasicReflectionTest, ErrorOnArgumentCountMismatch)
{
    auto args = ArgumentList::make(1, 2, 3); // addToAge只需要1个参数
    EXPECT_THROW(
        personClass->invoke("addToAge", &testPerson, args),
        std::runtime_error);
}
