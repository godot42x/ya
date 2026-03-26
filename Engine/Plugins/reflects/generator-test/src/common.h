#pragma once

#include <any>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "registry.h"

namespace refl
{
// 标记属性用于反射的属性标签
struct property
{};

struct serializable
{};

struct range
{};

struct uclass
{};

} // namespace refl

// ============================================================================
// 示例类：带有反射属性标记
// ============================================================================


struct
    [[refl::uclass]] Person
{
    [[refl::property]]
    std::string name;

    [[refl::property]]
    int age;

    [[refl::property]]
    float height;

    Person() : name(""), age(0), height(0.0f) {}
    Person(const std::string& n, int a, float h) : name(n), age(a), height(h) {}

    void introduce() const
    {
        // 方法实现
    }

    [[refl::serializable]]
    std::string serialize() const
    {
        return name + "," + std::to_string(age) + "," + std::to_string(height);
    }
};

struct
    [[refl::uclass]] Vehicle
{
    [[refl::property]]
    std::string brand;

    [[refl::property]] [[refl::range(min = 1900, max = 2100)]]
    int year;

    [[refl::property]]
    float price;

    Vehicle() : brand("Unknown"), year(2020), price(0.0f) {}
};

// 包含自动生成的反射注册代码
#include "common.generated.h"