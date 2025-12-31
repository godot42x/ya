#include "ReflectionSystem.h"

#include "Core/Log.h"
#include "ECS/Component/CameraComponent.h"
#include "ECS/Component/TransformComponent.h"



namespace ya
{

void ReflectionSystem::registerAllComponents()
{
    YA_CORE_TRACE("Registering component reflection data...");

    // 注册所有组件的反射信息
    TransformComponent::registerReflection();
    CameraComponent::registerReflection();
    PointLightComponent::registerReflection();

    // TODO: 添加更多组件
    // registerComponent<MeshComponent>();
    // registerComponent<MaterialComponent>();
    // registerComponent<ScriptComponent>();

    YA_CORE_INFO("Component reflection registration complete");
}

ReflectionSystem *ReflectionSystem::get()
{
    static ReflectionSystem instance;
    return &instance;
}

} // namespace ya
