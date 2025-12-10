#pragma once

#include "class.h"
#include "enum.h"
#include "lib.h"


#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>

struct ClassRegistry
{
    std::unordered_map<std::string, std::shared_ptr<Class>> classes;

    static ClassRegistry &instance();

    std::shared_ptr<Class> registerClass(const std::string &name, Class *classInfo)
    {
        classes[name] = std::shared_ptr<Class>(classInfo);
        return classes[name];
    }

    Class *getClass(const std::string &name)
    {
        auto it = classes.find(name);
        return it != classes.end() ? it->second.get() : nullptr;
    }

    // 通过类名字符串动态创建实例
    template <typename... Args>
    void *createInstance(const std::string &className, Args &&...args)
    {
        Class *cls = getClass(className);
        if (!cls) {
            throw std::runtime_error("Class not found: " + className);
        }
        return cls->createInstance(std::forward<Args>(args)...);
    }

    // 销毁动态创建的实例
    void destroyInstance(const std::string &className, void *obj)
    {
        Class *cls = getClass(className);
        if (!cls) {
            throw std::runtime_error("Class not found: " + className);
        }
        cls->destroyInstance(obj);
    }

    // 检查类是否已注册
    bool hasClass(const std::string &name) const
    {
        return classes.find(name) != classes.end();
    }
};

// ============================================================================
// Register - Auto-registration helper
// ============================================================================
template <typename T>
struct Register
{
    Class *classInfo;


    Register(const std::string &className)
    {
        // 自动注册到全局注册表
        classInfo = new Class(className);
        classInfo = ClassRegistry::instance().registerClass(className, classInfo).get();
    }

    // virtual ~Register() {}

    template <typename FuncType>
    Register &function(const std::string &name, FuncType func)
    {
        classInfo->function(name, func);
        return *this;
    }

    // 注册静态函数
    template <typename Ret, typename... Args>
    Register &staticFunction(const std::string &name, Ret (*func)(Args...))
    {
        classInfo->staticFunction(name, func);
        return *this;
    }

    // 统一的property函数 - 通过静态反射自动判断成员类型
    template <typename PropertyType>
    Register &property(const std::string &name, PropertyType prop)
    {
        using DecayedType = std::decay_t<PropertyType>;

        // 检查是否是成员指针
        if constexpr (std::is_member_object_pointer_v<DecayedType>) {
            // 提取成员指针指向的类型
            using MemberType = typename std::remove_reference_t<
                decltype(std::declval<T>().*prop)>;

            // 禁止引用类型的反射
            static_assert(!std::is_reference_v<MemberType>,
                          "Reference type members are not supported in reflection. "
                          "Use pointer types instead.");

            // 成员指针：可以是 T::* 或 const T::*
            classInfo->property(name, prop);
        }
        // 检查是否是指针类型（静态变量）
        else if constexpr (std::is_pointer_v<DecayedType>) {
            // 提取指针指向的类型
            using PointeeType = std::remove_pointer_t<DecayedType>;
            using BaseType    = std::remove_const_t<PointeeType>;

            // 禁止引用类型的反射
            static_assert(!std::is_reference_v<BaseType>,
                          "Reference type static variables are not supported in reflection. "
                          "Use pointer types instead.");

            // 静态变量指针：可以是 T* 或 const T*
            classInfo->staticProperty(name, prop);
        }
        else {
            static_assert(std::is_member_object_pointer_v<DecayedType> || std::is_pointer_v<DecayedType>,
                          "Property must be either a member pointer (T::*) or a static variable pointer");
        }

        return *this;
    }

    // 注册构造函数（根据参数类型自动推导）
    template <typename... Args>
    Register &constructor()
    {
        classInfo->registerConstructor<T, Args...>();
        return *this;
    }
};

// ============================================================================
// Test Example
// ============================================================================
inline void test()
{
    struct Person
    {
        std::string name;
        int         age;

        // 普通成员函数
        int display(int arg1)
        {
            printf("Person: %s, age: %d, arg1: %d\n", name.c_str(), age, arg1);
            return age + arg1;
        }

        // void 返回值函数
        void setInfo(const std::string &n, int a)
        {
            name = n;
            age  = a;
        }

        // const 成员函数
        std::string getName() const { return name; }

        int getAge() const { return age; }

        // 静态成员函数
        static int multiply(int a, int b) { return a * b; }

        static void printMessage(const std::string &msg)
        {
            printf("Message: %s\n", msg.c_str());
        }
    };

    // 静态变量用于测试
    static int       staticCounter    = 100;
    static const int staticConstValue = 42;

    // 使用Register进行自动注册 - 统一的property API
    Register<Person>("Person")
        .property("name", &Person::name)           // 普通成员变量
        .property("age", &Person::age)             // 普通成员变量
        .property("counter", &staticCounter)       // 静态变量
        .property("constValue", &staticConstValue) // const静态变量
        .function("display", &Person::display)
        .function("setInfo", &Person::setInfo)
        .function("getName", &Person::getName)
        .function("getAge", &Person::getAge);

    // 创建反射类
    Class personClass;

    // 注册成员函数
    personClass.function("display", &Person::display);
    personClass.function("setInfo", &Person::setInfo);
    personClass.function("getName", &Person::getName);
    personClass.function("getAge", &Person::getAge);

    // 注册静态函数
    personClass.staticFunction("multiply", &Person::multiply);
    personClass.staticFunction("printMessage", &Person::printMessage);

    printf("=== Reflection Test ===\n\n");

    // 创建测试对象
    Person alice{"Alice", 30};

    // 测试1：调用成员函数 - 使用底层 invoke
    printf("Test 1: invoke with ArgumentList\n");
    auto args1  = ArgumentList::make(5);
    auto result = personClass.invoke("display", &alice, args1);
    printf("Result: %d\n\n", std::any_cast<int>(result));

    // 测试2：调用成员函数 - 使用高层 call（推荐）
    printf("Test 2: call<int>\n");
    int ret = personClass.call<int>("display", &alice, 10);
    printf("Result: %d\n\n", ret);

    // 测试3：调用 void 返回值的函数
    printf("Test 3: call<void>\n");
    personClass.call<void>("setInfo", &alice, std::string("Bob"), 25);
    printf("Name changed to: %s, age: %d\n\n", alice.name.c_str(), alice.age);

    // 测试4：调用 const 成员函数
    printf("Test 4: const member function\n");
    std::string name = personClass.call<std::string>("getName", &alice);
    int         age  = personClass.call<int>("getAge", &alice);
    printf("Name: %s, Age: %d\n\n", name.c_str(), age);

    // 测试5：调用静态函数
    printf("Test 5: static function\n");
    int product = personClass.callStatic<int>("multiply", 6, 7);
    printf("6 * 7 = %d\n", product);
    personClass.callStatic<void>("printMessage", std::string("Hello Reflection!"));
    printf("\n");

    // 测试6：查询函数信息
    printf("Test 6: function introspection\n");
    if (personClass.hasFunction("display")) {
        const Function *f = personClass.getFunction("display");
        printf("Function 'display': args=%zu, return=%s\n", f->argCount, f->returnTypeName.c_str());
    }

    // 测试7：通过Register访问属性
    printf("\nTest 7: property access via Register\n");
    auto *registeredClass = ClassRegistry::instance().getClass("Person");
    if (registeredClass) {
        Person testPerson{"Charlie", 35};

        // 读取成员属性
        if (registeredClass->hasProperty("name")) {
            const Property *nameProp = registeredClass->getProperty("name");
            std::string     nameVal  = nameProp->getValue<std::string>(&testPerson);
            printf("name: %s (type: %s, const: %d, static: %d)\n",
                   nameVal.c_str(),
                   nameProp->getTypeName().c_str(),
                   nameProp->bConst,
                   nameProp->bStatic);
        }

        if (registeredClass->hasProperty("age")) {
            const Property *ageProp = registeredClass->getProperty("age");
            int             ageVal  = ageProp->getValue<int>(&testPerson);
            printf("age: %d (type: %s, const: %d, static: %d)\n",
                   ageVal,
                   ageProp->getTypeName().c_str(),
                   ageProp->bConst,
                   ageProp->bStatic);
        }

        // 写入成员属性
        if (registeredClass->hasProperty("name")) {
            Property *nameProp = registeredClass->getProperty("name");
            nameProp->setValue(&testPerson, std::string("David"));
            printf("After setting name: %s\n", testPerson.name.c_str());
        }

        // 读取静态属性
        if (registeredClass->hasProperty("counter")) {
            const Property *counterProp = registeredClass->getProperty("counter");
            int             counterVal  = counterProp->getValue<int>(nullptr); // 静态属性不需要对象实例
            printf("counter: %d (static: %d)\n", counterVal, counterProp->bStatic);
        }

        // 读取const静态属性
        if (registeredClass->hasProperty("constValue")) {
            const Property *constProp = registeredClass->getProperty("constValue");
            int             constVal  = constProp->getValue<int>(nullptr);
            printf("constValue: %d (const: %d, static: %d)\n",
                   constVal,
                   constProp->bConst,
                   constProp->bStatic);
        }
    }

    printf("\n=== All Tests Passed ===\n");
}


// ============================================================================
// EnumRegistry - 全局枚举注册表
// ============================================================================
struct EnumRegistry
{
    std::unordered_map<std::string, Enum> enums;

    static EnumRegistry &instance();

    void registerEnum(const std::string &enumName, const Enum &enumInfo)
    {
        enums[enumName] = enumInfo;
    }

    Enum *getEnum(const std::string &enumName)
    {
        auto it = enums.find(enumName);
        return it != enums.end() ? &it->second : nullptr;
    }

    bool hasEnum(const std::string &enumName) const
    {
        return enums.find(enumName) != enums.end();
    }
};

// ============================================================================
// RegisterEnum - 枚举注册辅助类
// ============================================================================
template <typename EnumType>
struct RegisterEnum
{
    Enum enumInfo;

    RegisterEnum(const std::string &enumName)
    {
        static_assert(std::is_enum_v<EnumType>, "T must be an enum type");
        enumInfo.name = enumName;
    }

    RegisterEnum &value(const std::string &valueName, EnumType val)
    {
        enumInfo.addValue(valueName, static_cast<int64_t>(val));
        return *this;
    }

    ~RegisterEnum()
    {
        EnumRegistry::instance().registerEnum(enumInfo.name, enumInfo);
    }
};
