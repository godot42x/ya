#include "IRenderSystem.h"

#include "Runtime/Application/App.h"
#include "ECS/Component/CameraComponent.h"
#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Render/Render.h"
#include "Scene/SceneManager.h"


namespace ya
{

App* IRenderSystem::getApp() const
{
    return App::get();
}

Scene* IRenderSystem::getActiveScene() const
{
    if (getApp()) {
        return getApp()->getSceneManager()->getActiveScene();
    }
    return nullptr;
}

IRender* IRenderSystem::getRender() const
{
    if (getApp()) {
        return getApp()->getRenderServices().getRender();
    }
    return nullptr;
}



} // namespace ya
