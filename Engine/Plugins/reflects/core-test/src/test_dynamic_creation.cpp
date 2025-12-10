#include "test_common.h"

// ============================================================================
// Dynamic Instance Creation Tests - 模拟Unreal的NewObject
// ============================================================================

TEST(DynamicCreationTest, CreateInstanceByClassName)
{
    // 通过字符串类名创建实例（类似Unreal的NewObject）
    void* vehiclePtr = ClassRegistry::instance().createInstance(
        "Vehicle", 
        std::string("Tesla"), 
        2024, 
        50000.0f
    );
    
    ASSERT_NE(vehiclePtr, nullptr);
    
    // 转换为具体类型并验证
    Vehicle* vehicle = static_cast<Vehicle*>(vehiclePtr);
    EXPECT_EQ(vehicle->brand, "Tesla");
    EXPECT_EQ(vehicle->year, 2024);
    EXPECT_FLOAT_EQ(vehicle->price, 50000.0f);
    
    // 通过反射调用方法
    Class* vehicleClass = ClassRegistry::instance().getClass("Vehicle");
    ASSERT_NE(vehicleClass, nullptr);
    
    std::string info = vehicleClass->call<std::string>("getInfo", vehicle);
    EXPECT_FALSE(info.empty());
    
    // 销毁实例
    ClassRegistry::instance().destroyInstance("Vehicle", vehiclePtr);
}

TEST(DynamicCreationTest, CreateInstanceViaClass)
{
    // 获取类信息
    Class* vehicleClass = ClassRegistry::instance().getClass("Vehicle");
    ASSERT_NE(vehicleClass, nullptr);
    
    // 通过Class对象创建实例
    void* vehiclePtr = vehicleClass->createInstance(
        std::string("BMW"),
        2023,
        45000.0f
    );
    
    ASSERT_NE(vehiclePtr, nullptr);
    
    Vehicle* vehicle = static_cast<Vehicle*>(vehiclePtr);
    EXPECT_EQ(vehicle->brand, "BMW");
    EXPECT_EQ(vehicle->year, 2023);
    
    // 通过反射修改属性
    vehicleClass->setPropertyValue("price", vehicle, 48000.0f);
    EXPECT_FLOAT_EQ(vehicle->price, 48000.0f);
    
    // 销毁实例
    vehicleClass->destroyInstance(vehiclePtr);
}

TEST(DynamicCreationTest, CreateDefaultInstance)
{
    // 创建默认实例（无参构造函数）
    void* vehiclePtr = ClassRegistry::instance().createInstance("Vehicle");
    
    ASSERT_NE(vehiclePtr, nullptr);
    
    Vehicle* vehicle = static_cast<Vehicle*>(vehiclePtr);
    EXPECT_EQ(vehicle->brand, "Unknown");
    EXPECT_EQ(vehicle->year, 0);
    EXPECT_FLOAT_EQ(vehicle->price, 0.0f);
    
    ClassRegistry::instance().destroyInstance("Vehicle", vehiclePtr);
}

TEST(DynamicCreationTest, CreatePersonInstance)
{
    // 通过字符串类名创建Person实例
    void* personPtr = ClassRegistry::instance().createInstance(
        "Person",
        std::string("Alice"),
        30
    );
    
    ASSERT_NE(personPtr, nullptr);
    
    Person* person = static_cast<Person*>(personPtr);
    EXPECT_EQ(person->name, "Alice");
    EXPECT_EQ(person->age, 30);
    
    // 销毁实例
    ClassRegistry::instance().destroyInstance("Person", personPtr);
}

TEST(DynamicCreationTest, CheckCanCreateInstance)
{
    Class* vehicleClass = ClassRegistry::instance().getClass("Vehicle");
    ASSERT_NE(vehicleClass, nullptr);
    EXPECT_TRUE(vehicleClass->canCreateInstance());
    
    Class* personClass = ClassRegistry::instance().getClass("Person");
    ASSERT_NE(personClass, nullptr);
    EXPECT_TRUE(personClass->canCreateInstance());
}

TEST(DynamicCreationTest, FullWorkflow)
{
    // 完整工作流：创建、修改、调用、销毁
    
    // 1. 创建实例
    void* obj = ClassRegistry::instance().createInstance(
        "Vehicle",
        std::string("Ford"),
        2022,
        35000.0f
    );
    
    ASSERT_NE(obj, nullptr);
    Vehicle* vehicle = static_cast<Vehicle*>(obj);
    
    // 2. 通过反射读取属性
    Class* cls = ClassRegistry::instance().getClass("Vehicle");
    std::string brand = cls->getPropertyValue<std::string>("brand", vehicle);
    int year = cls->getPropertyValue<int>("year", vehicle);
    float price = cls->getPropertyValue<float>("price", vehicle);
    
    EXPECT_EQ(brand, "Ford");
    EXPECT_EQ(year, 2022);
    EXPECT_FLOAT_EQ(price, 35000.0f);
    
    // 3. 通过反射修改属性
    cls->setPropertyValue("brand", vehicle, std::string("Ford Mustang"));
    cls->setPropertyValue("year", vehicle, 2023);
    
    // 4. 验证修改
    EXPECT_EQ(vehicle->brand, "Ford Mustang");
    EXPECT_EQ(vehicle->year, 2023);
    
    // 5. 通过反射调用方法
    std::string info = cls->call<std::string>("getInfo", vehicle);
    EXPECT_FALSE(info.empty());
    
    // 6. 销毁实例
    cls->destroyInstance(obj);
}

TEST(DynamicCreationTest, ErrorOnClassNotFound)
{
    EXPECT_THROW(
        ClassRegistry::instance().createInstance("NonExistentClass"),
        std::runtime_error
    );
}

TEST(DynamicCreationTest, MultipleConstructorOverloads)
{
    // 测试多个构造函数重载
    Class* vehicleClass = ClassRegistry::instance().getClass("Vehicle");
    ASSERT_NE(vehicleClass, nullptr);
    
    // 使用默认构造函数
    void* v1 = vehicleClass->createInstance();
    Vehicle* vehicle1 = static_cast<Vehicle*>(v1);
    EXPECT_EQ(vehicle1->brand, "Unknown");
    
    // 使用带参数的构造函数
    void* v2 = vehicleClass->createInstance(std::string("Honda"), 2023, 30000.0f);
    Vehicle* vehicle2 = static_cast<Vehicle*>(v2);
    EXPECT_EQ(vehicle2->brand, "Honda");
    EXPECT_EQ(vehicle2->year, 2023);
    
    // 清理
    vehicleClass->destroyInstance(v1);
    vehicleClass->destroyInstance(v2);
}
