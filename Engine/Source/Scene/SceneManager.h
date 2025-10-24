#pragma once

#include "Core/Base.h"
#include "Scene/Scene.h"
#include <functional>
#include <memory>
#include <string>

namespace ya
{

/**
 * @brief SceneManager - Manages scene lifecycle and transitions
 * 
 * Responsibilities:
 * - Load and unload scenes
 * - Scene transitions
 * - Provide callbacks for custom scene initialization
 */
class SceneManager
{
public:
    using SceneInitCallback = std::function<void(Scene*)>;

    SceneManager() = default;
    ~SceneManager();

    /**
     * @brief Load a scene from path
     * @param path The path to the scene file
     * @return true if loaded successfully, false otherwise
     */
    bool loadScene(const std::string& path);

    /**
     * @brief Unload the current scene
     * @return true if unloaded successfully, false otherwise
     */
    bool unloadScene();

    /**
     * @brief Get the current active scene
     */
    Scene* getCurrentScene() const { return _currentScene.get(); }

    /**
     * @brief Check if a scene is currently loaded
     */
    bool hasScene() const { return _currentScene != nullptr; }

    /**
     * @brief Set callback for custom scene initialization
     * This callback will be called after a scene is loaded
     */
    void setSceneInitCallback(SceneInitCallback callback) 
    { 
        _onSceneInit = callback; 
    }

    /**
     * @brief Set callback for scene cleanup
     * This callback will be called before a scene is unloaded
     */
    void setSceneCleanupCallback(SceneInitCallback callback)
    {
        _onSceneCleanup = callback;
    }

private:
    std::unique_ptr<Scene> _currentScene;
    SceneInitCallback _onSceneInit;
    SceneInitCallback _onSceneCleanup;
    std::string _currentScenePath;
};

} // namespace ya
