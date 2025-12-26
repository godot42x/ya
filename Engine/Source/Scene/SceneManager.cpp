#include "SceneManager.h"
#include "Core/System/FileSystem.h"
#include "Core/Log.h"
#include "Core/Serialization/SceneSerializer.h"

namespace ya
{

SceneManager::~SceneManager()
{
    unloadScene();
}

bool SceneManager::loadScene(const std::string &path)
{
    // Unload existing scene first
    if (_currentScene) {
        YA_CORE_WARN("Scene already loaded, unloading previous scene: {}", _currentScenePath);
        unloadScene();
    }

    // FileSystem::get()->getGameRoot()

    // Create new scene
    _currentScene     = std::make_unique<Scene>();
    _currentScenePath = path;

    // Call initialization callback if set
    if (_onSceneInit) {
        _onSceneInit(_currentScene.get());
    }

    YA_CORE_INFO("Scene loaded: {}", path);
    return true;
}

bool SceneManager::unloadScene()
{
    if (!_currentScene) {
        return false;
    }

    // Call cleanup callback if set
    if (_onSceneCleanup) {
        _onSceneCleanup(_currentScene.get());
    }

    _currentScene.reset();
    YA_CORE_INFO("Scene unloaded: {}", _currentScenePath);
    _currentScenePath.clear();

    return true;
}

void SceneManager::serializeToFile(const std::string &path, Scene *scene) const
{
    if (!scene) {
        YA_CORE_WARN("No scene loaded to serialize");
        return;
    }

    SceneSerializer serializer(scene);
    if (serializer.saveToFile(path)) {
        YA_CORE_INFO("Scene serialized to file: {}", path);
    }
    else {
        YA_CORE_ERROR("Failed to serialize scene to file: {}", path);
    }
}

void SceneManager::deserializeFromFile(const std::string &path, Scene *scene)
{
    if (!scene) {
        YA_CORE_WARN("No scene provided to deserialize into");
        return;
    }

    SceneSerializer serializer(scene);
    if (serializer.loadFromFile(path)) {
        YA_CORE_INFO("Scene deserialized from file: {}", path);
    }
    else {
        YA_CORE_ERROR("Failed to deserialize scene from file: {}", path);
    }
}

} // namespace ya
