
#pragma once

#include "Core/Serialization/SceneSerializer.h"
#include "Core/System/AutoRegister.h"
#include "Core/System/System.h"

namespace ya
{

/**
 * @brief 组件注册中心 - 自动注册所有组件的反射信息
 *
 * 组件通过 REFLECT_AUTO_REGISTER 宏自动注册，无需手动修改此类
 */
class ReflectionSystem
{
  public:
    static ReflectionSystem *get();

    void init()
    {
        // 执行所有自动注册的组件反射
        AutoRegisterRegistry::get().executeAll();
    }
};

} // namespace ya
