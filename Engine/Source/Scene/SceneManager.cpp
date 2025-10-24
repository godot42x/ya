#include "SceneManager.h"
#include "Core/Log.h"

namespace ya
{

SceneManager::~SceneManager()
{
    unloadScene();
}

bool SceneManager::loadScene(const std::string& path)
{
    // Unload existing scene first
    if (_currentScene) {
        YA_CORE_WARN("Scene already loaded, unloading previous scene: {}", _currentScenePath);
        unloadScene();
    }

    // Create new scene
    _currentScene = std::make_unique<Scene>();
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

} // namespace ya
