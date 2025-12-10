#pragma once

#include "reflects-core/lib.h"
#include <gtest/gtest.h>
#include <string>

// ============================================================================
// Common Test Classes
// ============================================================================

// 简单的Person类用于测试
class Person
{
  public:
    std::string name;
    int         age;

    Person(const std::string &n = "", int a = 0) : name(n), age(a) {}

    // 成员函数
    int addToAge(int value) { return age + value; }
    
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
};

// 带有静态成员的类
class ConfigManager
{
  public:
    inline static int maxConnections = 100;
    inline static const int defaultTimeout = 30;
    
    ConfigManager() = default;
};

// 用于测试多构造函数的类
class Vehicle
{
  public:
    std::string brand;
    int year;
    float price;

    // 默认构造函数
    Vehicle() : brand("Unknown"), year(0), price(0.0f) {}

    // 带参数的构造函数
    Vehicle(const std::string& b, int y, float p) 
        : brand(b), year(y), price(p) {}

    std::string getInfo() const
    {
        return brand + " (" + std::to_string(year) + ")";
    }
};

// ============================================================================
// 类型注册 - 模拟 Unreal Header Tool / Reflector
// ============================================================================
namespace
{
    struct PersonReflection
    {
        PersonReflection()
        {
            Register<Person>("Person")
                .property("name", &Person::name)
                .property("age", &Person::age)
                .function("addToAge", &Person::addToAge)
                .function("setInfo", &Person::setInfo)
                .function("getName", &Person::getName)
                .function("getAge", &Person::getAge)
                .staticFunction("multiply", &Person::multiply)
                .constructor()
                .constructor<std::string, int>();
        }
    };
    
    struct ConfigManagerReflection
    {
        ConfigManagerReflection()
        {
            Register<ConfigManager>("ConfigManager")
                .property("maxConnections", &ConfigManager::maxConnections)
                .property("defaultTimeout", &ConfigManager::defaultTimeout)
                .constructor();
        }
    };
    
    struct VehicleReflection
    {
        VehicleReflection()
        {
            Register<Vehicle>("Vehicle")
                .property("brand", &Vehicle::brand)
                .property("year", &Vehicle::year)
                .property("price", &Vehicle::price)
                .function("getInfo", &Vehicle::getInfo)
                .constructor()
                .constructor<std::string, int, float>();
        }
    };
    
    // 静态实例，在程序启动时自动注册
    static PersonReflection g_PersonReflection;
    static ConfigManagerReflection g_ConfigManagerReflection;
    static VehicleReflection g_VehicleReflection;
}

