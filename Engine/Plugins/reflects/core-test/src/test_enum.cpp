#include "test_common.h"
#include <gtest/gtest.h>

// ============================================================================
// Enum Test Cases
// ============================================================================

// 定义测试用枚举
enum class Color
{
    Red = 0,
    Green = 1,
    Blue = 2,
    Yellow = 3
};

enum class Priority : int
{
    Low = -1,
    Normal = 0,
    High = 1,
    Critical = 10
};

// 注册枚举
namespace
{
    struct ColorEnumReflection
    {
        ColorEnumReflection()
        {
            RegisterEnum<Color>("Color")
                .value("Red", Color::Red)
                .value("Green", Color::Green)
                .value("Blue", Color::Blue)
                .value("Yellow", Color::Yellow);
        }
    };
    
    struct PriorityEnumReflection
    {
        PriorityEnumReflection()
        {
            RegisterEnum<Priority>("Priority")
                .value("Low", Priority::Low)
                .value("Normal", Priority::Normal)
                .value("High", Priority::High)
                .value("Critical", Priority::Critical);
        }
    };
    
    static ColorEnumReflection g_ColorEnumReflection;
    static PriorityEnumReflection g_PriorityEnumReflection;
}

// ============================================================================
// Test Cases
// ============================================================================

TEST(EnumReflectionTest, EnumRegistration)
{
    // 检查枚举是否已注册
    EXPECT_TRUE(EnumRegistry::instance().hasEnum("Color"));
    EXPECT_TRUE(EnumRegistry::instance().hasEnum("Priority"));
    EXPECT_FALSE(EnumRegistry::instance().hasEnum("NonExistent"));
}

TEST(EnumReflectionTest, GetEnumByName)
{
    Enum* colorEnum = EnumRegistry::instance().getEnum("Color");
    ASSERT_NE(colorEnum, nullptr);
    EXPECT_EQ(colorEnum->name, "Color");
    
    Enum* priorityEnum = EnumRegistry::instance().getEnum("Priority");
    ASSERT_NE(priorityEnum, nullptr);
    EXPECT_EQ(priorityEnum->name, "Priority");
}

TEST(EnumReflectionTest, ValueToName)
{
    Enum* colorEnum = EnumRegistry::instance().getEnum("Color");
    ASSERT_NE(colorEnum, nullptr);
    
    EXPECT_EQ(colorEnum->getName(0), "Red");
    EXPECT_EQ(colorEnum->getName(1), "Green");
    EXPECT_EQ(colorEnum->getName(2), "Blue");
    EXPECT_EQ(colorEnum->getName(3), "Yellow");
}

TEST(EnumReflectionTest, NameToValue)
{
    Enum* colorEnum = EnumRegistry::instance().getEnum("Color");
    ASSERT_NE(colorEnum, nullptr);
    
    EXPECT_EQ(colorEnum->getValue("Red"), 0);
    EXPECT_EQ(colorEnum->getValue("Green"), 1);
    EXPECT_EQ(colorEnum->getValue("Blue"), 2);
    EXPECT_EQ(colorEnum->getValue("Yellow"), 3);
}

TEST(EnumReflectionTest, HasValue)
{
    Enum* colorEnum = EnumRegistry::instance().getEnum("Color");
    ASSERT_NE(colorEnum, nullptr);
    
    EXPECT_TRUE(colorEnum->hasName("Red"));
    EXPECT_TRUE(colorEnum->hasName("Green"));
    EXPECT_FALSE(colorEnum->hasName("Purple"));
    
    EXPECT_TRUE(colorEnum->hasValue(0));
    EXPECT_TRUE(colorEnum->hasValue(1));
    EXPECT_FALSE(colorEnum->hasValue(99));
}

TEST(EnumReflectionTest, NegativeValues)
{
    Enum* priorityEnum = EnumRegistry::instance().getEnum("Priority");
    ASSERT_NE(priorityEnum, nullptr);
    
    EXPECT_EQ(priorityEnum->getValue("Low"), -1);
    EXPECT_EQ(priorityEnum->getValue("Normal"), 0);
    EXPECT_EQ(priorityEnum->getValue("High"), 1);
    EXPECT_EQ(priorityEnum->getValue("Critical"), 10);
    
    EXPECT_EQ(priorityEnum->getName(-1), "Low");
    EXPECT_EQ(priorityEnum->getName(10), "Critical");
}

TEST(EnumReflectionTest, GetAllValues)
{
    Enum* colorEnum = EnumRegistry::instance().getEnum("Color");
    ASSERT_NE(colorEnum, nullptr);
    
    const auto& values = colorEnum->getValues();
    EXPECT_EQ(values.size(), 4);
    
    // 验证所有值都在列表中
    bool foundRed = false, foundGreen = false, foundBlue = false, foundYellow = false;
    for (const auto& val : values) {
        if (val.name == "Red" && val.value == 0) foundRed = true;
        if (val.name == "Green" && val.value == 1) foundGreen = true;
        if (val.name == "Blue" && val.value == 2) foundBlue = true;
        if (val.name == "Yellow" && val.value == 3) foundYellow = true;
    }
    
    EXPECT_TRUE(foundRed);
    EXPECT_TRUE(foundGreen);
    EXPECT_TRUE(foundBlue);
    EXPECT_TRUE(foundYellow);
}

TEST(EnumReflectionTest, ErrorOnInvalidValue)
{
    Enum* colorEnum = EnumRegistry::instance().getEnum("Color");
    ASSERT_NE(colorEnum, nullptr);
    
    EXPECT_THROW(colorEnum->getName(999), std::runtime_error);
    EXPECT_THROW(colorEnum->getValue("InvalidColor"), std::runtime_error);
}

TEST(EnumReflectionTest, ErrorOnNonexistentEnum)
{
    Enum* enumPtr = EnumRegistry::instance().getEnum("NonExistent");
    EXPECT_EQ(enumPtr, nullptr);
}

// ============================================================================
// 带枚举属性的类测试
// ============================================================================

class GameObject
{
public:
    std::string name;
    Color color;
    Priority priority;
    
    GameObject() : name("Object"), color(Color::Red), priority(Priority::Normal) {}
    GameObject(const std::string& n, Color c, Priority p) 
        : name(n), color(c), priority(p) {}
};

namespace
{
    struct GameObjectReflection
    {
        GameObjectReflection()
        {
            Register<GameObject>("GameObject")
                .property("name", &GameObject::name)
                .property("color", &GameObject::color)
                .property("priority", &GameObject::priority)
                .constructor()
                .constructor<std::string, Color, Priority>();
        }
    };
    
    static GameObjectReflection g_GameObjectReflection;
}

TEST(EnumReflectionTest, EnumAsClassProperty)
{
    Class* gameObjectClass = ClassRegistry::instance().getClass("GameObject");
    ASSERT_NE(gameObjectClass, nullptr);
    
    // 创建对象
    GameObject obj("TestObject", Color::Blue, Priority::High);
    
    // 通过反射读取枚举属性
    Color color = gameObjectClass->getPropertyValue<Color>("color", &obj);
    Priority priority = gameObjectClass->getPropertyValue<Priority>("priority", &obj);
    
    EXPECT_EQ(color, Color::Blue);
    EXPECT_EQ(priority, Priority::High);
    
    // 通过反射设置枚举属性
    gameObjectClass->setPropertyValue("color", &obj, Color::Green);
    gameObjectClass->setPropertyValue("priority", &obj, Priority::Critical);
    
    EXPECT_EQ(obj.color, Color::Green);
    EXPECT_EQ(obj.priority, Priority::Critical);
}

TEST(EnumReflectionTest, EnumValueConversion)
{
    Enum* colorEnum = EnumRegistry::instance().getEnum("Color");
    ASSERT_NE(colorEnum, nullptr);
    
    // 创建对象并设置属性
    GameObject obj;
    obj.color = Color::Yellow;
    
    // 将枚举值转换为名称
    int64_t colorValue = static_cast<int64_t>(obj.color);
    std::string colorName = colorEnum->getName(colorValue);
    EXPECT_EQ(colorName, "Yellow");
    
    // 将名称转换回枚举值
    int64_t blueValue = colorEnum->getValue("Blue");
    obj.color = static_cast<Color>(blueValue);
    EXPECT_EQ(obj.color, Color::Blue);
}

TEST(EnumReflectionTest, CreateObjectWithEnumConstructor)
{
    // 通过反射创建带枚举参数的对象
    void* objPtr = ClassRegistry::instance().createInstance(
        "GameObject",
        std::string("DynamicObject"),
        Color::Red,
        Priority::Critical
    );
    
    ASSERT_NE(objPtr, nullptr);
    
    GameObject* obj = static_cast<GameObject*>(objPtr);
    EXPECT_EQ(obj->name, "DynamicObject");
    EXPECT_EQ(obj->color, Color::Red);
    EXPECT_EQ(obj->priority, Priority::Critical);
    
    ClassRegistry::instance().destroyInstance("GameObject", objPtr);
}
