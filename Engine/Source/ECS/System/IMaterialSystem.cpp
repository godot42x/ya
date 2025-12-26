#include "IMaterialSystem.h"

#include "Core/App/App.h"
#include "ECS/Component/CameraComponent.h"
#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Render/Core/IRenderTarget.h"
#include "Render/Render.h"



#include "imgui.h"

namespace ya
{


App *IMaterialSystem::getApp() const
{
    return App::get();
}

Scene *IMaterialSystem::getScene() const
{
    if (getApp()) {
        return getApp()->getScene();
    }
    return nullptr;
}

IRender *IMaterialSystem::getRender() const
{
    if (getApp()) {
        return getApp()->getRender();
    }
    return nullptr;
}



} // namespace ya
