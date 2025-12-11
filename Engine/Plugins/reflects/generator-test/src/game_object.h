#pragma once

#include <string>
#include <vector>

// 包含反射标记
#include "common.h"

// ============================================================================
// 游戏对象示例类
// ============================================================================

struct [[refl::uclass]] GameObject
{
    [[refl::property]]
    std::string name;

    [[refl::property]]
    int id;

    [[refl::property]]
    bool active;

    [[refl::property]]
    bool anotherProp = 1;

    GameObject() : name("GameObject"), id(0), active(true) {}

    GameObject(const std::string &n, int i) : name(n), id(i), active(true) {}

    void activate()
    {
        active = true;
    }

    void deactivate()
    {
        active = false;
    }

    [[refl::serializable]]
    std::string toString() const
    {
        return name + " (ID: " + std::to_string(id) + ", Active: " + (active ? "true" : "false") + ")";
    }
};

// ============================================================================
// 组件示例类
// ============================================================================

struct [[refl::uclass]] Component
{
    [[refl::property]]
    std::string componentName;

    [[refl::property]]
    bool enabled;

    Component() : componentName("Component"), enabled(true) {}

    void enable() { enabled = true; }
    void disable() { enabled = false; }
};

// 包含自动生成的反射注册代码
#if __has_include("game_object.generated.h")
    #include "game_object.generated.h"
#endif
