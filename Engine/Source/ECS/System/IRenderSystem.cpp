#include "IRenderSystem.h"

#include "Core/App/App.h"
#include "ECS/Component/CameraComponent.h"
#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Render/Core/IRenderTarget.h"
#include "Render/Render.h"
#include "Scene/SceneManager.h"



#include "imgui.h"

namespace ya
{


App *IRenderSystem::getApp() const
{
    return App::get();
}

Scene *IRenderSystem::getActiveScene() const
{
    if (getApp()) {
        return getApp()->getSceneManager()->getActiveScene();
    }
    return nullptr;
}

IRender *IRenderSystem::getRender() const
{
    if (getApp()) {
        return getApp()->getRender();
    }
    return nullptr;
}



} // namespace ya
