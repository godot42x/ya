
#pragma once



#include "Core/Serialization/SceneSerializer.h"
#include "Core/System/System.h"

namespace ya
{

/**
 * @brief 组件注册中心 - 负责注册所有组件的反射信息
 *
 * 在应用启动时调用 registerAllComponents() 来注册所有组件的反射信息
 */
class ReflectionSystem
{
  public:
    /**
     * 注册所有组件的反射信息
     * 应该在 App::onInit() 中调用一次
     */
    static void registerAllComponents();

    static void init()
    {
        ReflectionSystem::registerAllComponents();
        SceneSerializer::registerComponentSerializers();
    }
};

} // namespace ya
