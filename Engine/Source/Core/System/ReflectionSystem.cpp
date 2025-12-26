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

    // TODO: 添加更多组件
    // MeshComponent::registerReflection();
    // MaterialComponent::registerReflection();
    // ScriptComponent::registerReflection();

    YA_CORE_INFO("Component reflection registration complete");
}

} // namespace ya
