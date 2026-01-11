#pragma once

#include "Core/Base.h"
#include "Core/Delegate.h"
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
struct SceneManager
{
  public:
    using SceneInitCallback = std::function<void(Scene *)>;

  private:
    stdptr<Scene> _currentScene;
    stdptr<Scene> _editorScene = nullptr;
    // std::string   _currentScenePath;

  public:
    /**
      State:
      Engine Start->
      SceneManager created ->
      Open scene ->
      Scene initialized (onSceneInit) ->
      Engine running ->
      Close scene (onSceneDestroy) -> Unload scene -> Engine Quit
    */

    // MulticastDelegate<void(Scene *)> onSceneInit;
    MulticastDelegate<void(Scene *)> onSceneDestroy;
    MulticastDelegate<void(Scene *)> onSceneActivated;

  public:

    SceneManager() = default;
    ~SceneManager();

    /**
     * @brief Load a scene from path
     * @param path The path to the scene file
     * @return true if loaded successfully, false otherwise
     */
    bool loadScene(const std::string &path);

    bool unloadScene();

    void                 setActiveScene(stdptr<Scene> scene);
    [[nodiscard]] Scene *getActiveScene() const { return _currentScene.get(); }
    [[nodiscard]] Scene *getEditorScene() const { return _editorScene.get(); }
    bool                 hasScene() const { return _currentScene != nullptr; }


    void serializeToFile(const std::string &path, Scene *scene) const;
    void deserializeFromFile(const std::string &path, Scene *scene);
    void setCurrentScene(stdptr<Scene> scene) { _currentScene = scene; }

    void onStartRuntime();
    void onStopRuntime();

    bool isSceneValid(Scene *ptr);

    stdptr<Scene> cloneScene(Scene *scene) const;
};

} // namespace ya
