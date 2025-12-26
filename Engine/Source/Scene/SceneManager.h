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
class SceneManager
{
  public:
    using SceneInitCallback = std::function<void(Scene *)>;

  private:
    std::unique_ptr<Scene> _currentScene;
    std::string            _currentScenePath;

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

    MulticastDelegate<void(Scene *)> onSceneInit;
    MulticastDelegate<void(Scene *)> onSceneDestroy;

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

    [[nodiscard]] Scene *getCurrentScene() const { return _currentScene.get(); }
    bool                 hasScene() const { return _currentScene != nullptr; }


    void serializeToFile(const std::string &path, Scene *scene) const;
    void deserializeFromFile(const std::string &path, Scene *scene);
};

} // namespace ya
