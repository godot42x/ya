#include "IRenderSystem.h"

#include "Product/Host/App.h"
#include "Framework/Game/Gameplay/ECS/Component/CameraComponent.h"
#include "Foundation/RHI/Render.h"
#include "Framework/Game/Render/Render3D/SceneManager.h"


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
