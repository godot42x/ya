#include "GameRuntime/AppSceneServices.h"

#include "GameRuntime/App.h"
#include "GameRuntime/Render/SceneCameraQuery.h"
#include "GameRuntime/Script/GameplayScriptFunctions.h"
#include "Core/Log.h"
#include "ECS/Component/3D/EnvironmentLightingComponent.h"
#include "ECS/Component/3D/SkyboxComponent.h"
#include "ECS/Systems/LuaScriptingSystem.h"
#include "Render3D/EnvironmentLighting/EnvironmentLightingProcessor.h"
#include "Scene/Runtime/SceneManager.h"

#include <filesystem>

namespace ya
{

std::string resolveProjectScenePath(const App& app, const std::string& requestedPath)
{
    if (requestedPath.empty()) {
        return {};
    }

    const std::filesystem::path inputPath(requestedPath);
    if (std::filesystem::is_regular_file(inputPath)) {
        return inputPath.lexically_normal().string();
    }

    if (app.getDesc().projectRoot) {
        const auto rootedPath = std::filesystem::path(*app.getDesc().projectRoot) / inputPath;
        if (std::filesystem::is_regular_file(rootedPath)) {
            return rootedPath.lexically_normal().string();
        }
    }

    return requestedPath;
}

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
    return _app ? _app->loadSceneInternal(path) : false;
}

bool AppSceneServices::unloadScene()
{
    return _app ? _app->unloadSceneInternal() : false;
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
    registry.view<SkyboxComponent>().each([envProcessor, scene](auto entity, SkyboxComponent& skybox) {
        skybox.invalidate();
        if (envProcessor) {
            envProcessor->markSkyboxDirty(*scene, entity, "scene derived-state refresh");
        }
    });
    registry.view<EnvironmentLightingComponent>().each([envProcessor, scene](auto entity, EnvironmentLightingComponent& environment) {
        environment.invalidate();
        if (envProcessor) {
            envProcessor->markEnvironmentLightingDirty(*scene, entity, "scene derived-state refresh");
        }
    });
}

void AppSceneServices::refreshActiveSceneDerivedState()
{
    refreshSceneDerivedState(getActiveScene());
}

Entity* AppSceneServices::getPrimaryCamera() const
{
    Scene* scene = getActiveScene();
    return scene ? findPrimaryCamera(*scene) : nullptr;
}

void AppSceneServices::requestSceneTransfer(const std::string& path, const std::string& spawnName)
{
    if (path.empty()) {
        YA_CORE_WARN("world.loadScene: empty path");
        return;
    }
    // A frame's last request wins: one slot, no queue to drain.
    _pendingTransfer = FPendingTransfer{.path = path, .spawnName = spawnName};
}

void AppSceneServices::runPendingSceneTransfer()
{
    if (!_app || !_pendingTransfer) {
        return;
    }
    const FPendingTransfer transfer = *_pendingTransfer;
    _pendingTransfer.reset();

    App&  app   = *_app;
    auto* manager = getSceneManager();
    if (!manager) {
        YA_CORE_WARN("world.loadScene: no scene manager");
        return;
    }
    if (!app.isRuntimeMode() && !app.isSimulationMode()) {
        // The request can only originate from a running script; dropping it
        // here means the state moved on between request and flush.
        YA_CORE_WARN("world.loadScene: dropped, play is not running");
        return;
    }

    // The transfer keeps the play session: app state, the UI host and the
    // persistent script state are untouched. Only the old scene's scripts
    // stop (onStop), then the scene swaps (waitIdle first, like every scene
    // swap, because the previous frame's render work may still be in flight).
    if (auto* render = app.getRenderServices().getRender()) {
        render->waitIdle();
    }
    if (app._luaScriptingSystem) {
        app._luaScriptingSystem->onStop();
    }
    if (!manager->loadScene(resolveProjectScenePath(app, transfer.path))) {
        YA_CORE_ERROR("world.loadScene: failed to load '{}'", transfer.path);
        return;
    }
    if (!transfer.spawnName.empty()) {
        if (Scene* scene = getActiveScene()) {
            placePlayerAtSpawn(*scene, transfer.spawnName);
        }
    }
    YA_CORE_INFO("Scene transferred to '{}' (spawn '{}')", transfer.path, transfer.spawnName);
}

} // namespace ya
