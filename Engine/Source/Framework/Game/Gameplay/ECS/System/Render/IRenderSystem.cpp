#include "IRenderSystem.h"

#include "Host/App.h"
#include "ECS/Component/CameraComponent.h"
#include "RHI/Render.h"
#include "Scene/Runtime/SceneManager.h"


namespace ya
{

App* IRenderSystem::getApp() const
{
    return App::get();
}

Scene* IRenderSystem::getActiveScene() const
{
    if (getApp()) {
        return getApp()->getSceneServices().getActiveScene();
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
