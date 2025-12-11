#include "common.h"
#include "game_object.h"
#include <gtest/gtest.h>


// ============================================================================
// 测试：动态对象创建
// ============================================================================

TEST(ReflectorTest, CreatePersonInstance)
{
    // 通过反射创建 Person 实例
    Class *personClass = ClassRegistry::instance().getClass("Person");
    ASSERT_NE(personClass, nullptr) << "Person class should be registered";

    // 使用默认构造函数
    void *obj = personClass->createInstance();
    ASSERT_NE(obj, nullptr);

    Person *person = static_cast<Person *>(obj);
    EXPECT_EQ(person->name, "");
    EXPECT_EQ(person->age, 0);
    EXPECT_FLOAT_EQ(person->height, 0.0f);

    personClass->destroyInstance(obj);
}

TEST(ReflectorTest, CreatePersonWithArgs)
{
    Class *personClass = ClassRegistry::instance().getClass("Person");
    ASSERT_NE(personClass, nullptr);

    // 使用带参数的构造函数
    // ArgumentList args = ArgumentList::make(std::string("Alice"), 25, 165.5f);
    // void        *obj  = personClass->createInstance(args);
    void *obj = personClass->createInstance(std::string("Alice"), 25, 165.5f);
    ASSERT_NE(obj, nullptr);

    Person *person = static_cast<Person *>(obj);
    EXPECT_EQ(person->name, "Alice");
    EXPECT_EQ(person->age, 25);
    EXPECT_FLOAT_EQ(person->height, 165.5f);

    personClass->destroyInstance(obj);
}

// ============================================================================
// 测试：反射属性访问
// ============================================================================

TEST(ReflectorTest, GetProperty)
{
    Person person("Bob", 30, 180.0f);

    Class *personClass = ClassRegistry::instance().getClass("Person");
    ASSERT_NE(personClass, nullptr);

    // 读取属性
    Property *nameProp = personClass->getProperty("name");
    ASSERT_NE(nameProp, nullptr);
    std::string name = nameProp->getValue<std::string>(&person);
    EXPECT_EQ(name, "Bob");

    Property *ageProp = personClass->getProperty("age");
    ASSERT_NE(ageProp, nullptr);
    int age = ageProp->getValue<int>(&person);
    EXPECT_EQ(age, 30);
}

TEST(ReflectorTest, SetProperty)
{
    Person person("Charlie", 35, 175.0f);

    Class *personClass = ClassRegistry::instance().getClass("Person");
    ASSERT_NE(personClass, nullptr);

    // 修改属性
    Property *nameProp = personClass->getProperty("name");
    ASSERT_NE(nameProp, nullptr);
    nameProp->setValue(&person, std::string("David"));
    EXPECT_EQ(person.name, "David");

    Property *ageProp = personClass->getProperty("age");
    ASSERT_NE(ageProp, nullptr);
    ageProp->setValue(&person, 40);
    EXPECT_EQ(person.age, 40);
}

TEST(ReflectorTest, IterateProperties)
{
    Class *personClass = ClassRegistry::instance().getClass("Person");
    ASSERT_NE(personClass, nullptr);

    // 遍历所有属性
    EXPECT_EQ(personClass->properties.size(), 3);

    EXPECT_TRUE(personClass->properties.find("name") != personClass->properties.end());
    EXPECT_TRUE(personClass->properties.find("age") != personClass->properties.end());
    EXPECT_TRUE(personClass->properties.find("height") != personClass->properties.end());
}

// ============================================================================
// 测试：反射方法调用
// ============================================================================

TEST(ReflectorTest, CallMethod)
{
    Person person("Eve", 28, 170.0f);

    Class *personClass = ClassRegistry::instance().getClass("Person");
    ASSERT_NE(personClass, nullptr);

    // 调用方法
    const Function *serializeFunc = personClass->getFunction("serialize");
    ASSERT_NE(serializeFunc, nullptr);

    ArgumentList args;
    std::any     result     = serializeFunc->invoker(&person, args);
    std::string  serialized = std::any_cast<std::string>(result);

    EXPECT_EQ(serialized, "Eve,28,170.000000");
}

// ============================================================================
// 测试：Vehicle 类反射
// ============================================================================

TEST(ReflectorTest, CreateVehicle)
{
    Class *vehicleClass = ClassRegistry::instance().getClass("Vehicle");
    ASSERT_NE(vehicleClass, nullptr) << "Vehicle class should be registered";

    void *obj = vehicleClass->createInstance();
    ASSERT_NE(obj, nullptr);

    Vehicle *vehicle = static_cast<Vehicle *>(obj);
    EXPECT_EQ(vehicle->brand, "Unknown");
    EXPECT_EQ(vehicle->year, 2020);
    EXPECT_FLOAT_EQ(vehicle->price, 0.0f);

    vehicleClass->destroyInstance(obj);
}

TEST(ReflectorTest, VehicleProperties)
{
    Vehicle vehicle;
    vehicle.brand = "Tesla";
    vehicle.year  = 2024;
    vehicle.price = 50000.0f;

    Class *vehicleClass = ClassRegistry::instance().getClass("Vehicle");
    ASSERT_NE(vehicleClass, nullptr);

    // 读取属性
    Property *brandProp = vehicleClass->getProperty("brand");
    ASSERT_NE(brandProp, nullptr);
    std::string brand = brandProp->getValue<std::string>(&vehicle);
    EXPECT_EQ(brand, "Tesla");

    Property *yearProp = vehicleClass->getProperty("year");
    ASSERT_NE(yearProp, nullptr);
    int year = yearProp->getValue<int>(&vehicle);
    EXPECT_EQ(year, 2024);

    Property *priceProp = vehicleClass->getProperty("price");
    ASSERT_NE(priceProp, nullptr);
    float price = priceProp->getValue<float>(&vehicle);
    EXPECT_FLOAT_EQ(price, 50000.0f);
}

// ============================================================================
// 测试：类注册检查
// ============================================================================

TEST(ReflectorTest, CheckRegisteredClasses)
{
    EXPECT_TRUE(ClassRegistry::instance().hasClass("Person"));
    EXPECT_TRUE(ClassRegistry::instance().hasClass("Vehicle"));
    EXPECT_FALSE(ClassRegistry::instance().hasClass("NonExistent"));
}

TEST(ReflectorTest, GetNonExistentClass)
{
    Class *cls = ClassRegistry::instance().getClass("DoesNotExist");
    EXPECT_EQ(cls, nullptr);
}

// ============================================================================
// 测试：属性类型检查
// ============================================================================

TEST(ReflectorTest, PropertyTypeCheck)
{
    Class *personClass = ClassRegistry::instance().getClass("Person");
    ASSERT_NE(personClass, nullptr);

    Property *nameProp = personClass->getProperty("name");
    ASSERT_NE(nameProp, nullptr);
    EXPECT_TRUE(nameProp->isType<std::string>());
    EXPECT_FALSE(nameProp->isType<int>());

    Property *ageProp = personClass->getProperty("age");
    ASSERT_NE(ageProp, nullptr);
    EXPECT_TRUE(ageProp->isType<int>());
    EXPECT_FALSE(ageProp->isType<std::string>());
}
