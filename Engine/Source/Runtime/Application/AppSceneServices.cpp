#include "Runtime/Application/AppSceneServices.h"

#include "Runtime/Application/App.h"
#include "Runtime/Application/Lifecycle/AppFrameLoop.h"
#include "Runtime/Application/Lifecycle/AppLifecycle.h"

#include "Core/Log.h"
#include "ECS/Component/3D/EnvironmentLightingComponent.h"
#include "ECS/Component/3D/SkyboxComponent.h"
#include "Scene/SceneManager.h"

namespace ya
{

SceneManager* AppSceneServices::getSceneManager() const
{
    return _app ? _app->_sceneManager : nullptr;
}

Scene* AppSceneServices::getActiveScene() const
{
    auto* sceneManager = getSceneManager();
    return sceneManager ? sceneManager->getActiveScene() : nullptr;
}

bool AppSceneServices::hasScene() const
{
    auto* sceneManager = getSceneManager();
    return sceneManager && sceneManager->hasScene();
}

bool AppSceneServices::loadScene(const std::string& path)
{
    return _app ? AppLifecycle::loadScene(*_app, path) : false;
}

bool AppSceneServices::unloadScene()
{
    return _app ? AppLifecycle::unloadScene(*_app) : false;
}

bool AppSceneServices::saveScene(const std::string& path)
{
    if (!_app) {
        return false;
    }
    if (path.empty()) {
        YA_CORE_WARN("Cannot save scene: empty path");
        return false;
    }

    auto* sceneManager = getSceneManager();
    if (!sceneManager) {
        YA_CORE_WARN("Cannot save scene without a scene manager");
        return false;
    }

    Scene* scene = sceneManager->getActiveScene();
    if (!scene) {
        YA_CORE_WARN("Cannot save scene: no active scene");
        return false;
    }

    return sceneManager->serializeToFile(path, scene);
}

void AppSceneServices::refreshSceneDerivedState(Scene* scene)
{
    if (!scene) {
        return;
    }

    auto& registry = scene->getRegistry();
    registry.view<SkyboxComponent>().each([](auto, SkyboxComponent& skybox) {
        skybox.invalidate();
    });
    registry.view<EnvironmentLightingComponent>().each([](auto, EnvironmentLightingComponent& environment) {
        environment.invalidate();
    });
}

void AppSceneServices::refreshActiveSceneDerivedState()
{
    refreshSceneDerivedState(getActiveScene());
}

Entity* AppSceneServices::getPrimaryCamera() const
{
    return _app ? AppFrameLoop::getPrimaryCamera(*_app) : nullptr;
}

} // namespace ya
