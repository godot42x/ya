#include "GameRuntime/AppSceneServices.h"

#include "GameRuntime/App.h"
#include "GameRuntime/Lifecycle/AppFrameLoop.h"
#include "GameRuntime/Lifecycle/AppLifecycle.h"

#include "Core/Log.h"
#include "ECS/Component/3D/EnvironmentLightingComponent.h"
#include "ECS/Component/3D/SkyboxComponent.h"
#include "Render3D/EnvironmentLighting/EnvironmentLightingProcessor.h"
#include "Scene/Runtime/SceneManager.h"

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
    auto* envProcessor = _app ? _app->getEnvironmentLightingProcessor() : nullptr;
    registry.view<SkyboxComponent>().each([envProcessor](auto entity, SkyboxComponent& skybox) {
        skybox.invalidate();
        if (envProcessor) {
            envProcessor->markSkyboxDirty(entity, "scene derived-state refresh");
        }
    });
    registry.view<EnvironmentLightingComponent>().each([envProcessor](auto entity, EnvironmentLightingComponent& environment) {
        environment.invalidate();
        if (envProcessor) {
            envProcessor->markEnvironmentLightingDirty(entity, "scene derived-state refresh");
        }
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
