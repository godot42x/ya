#pragma once

#include "Core/Base.h"
#include "Core/Delegate.h"
#include "Scene/Scene.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>

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
    using SceneInitCallback = std::function<void(Scene*)>;

  private:
    stdptr<Scene> _activeScene = nullptr;
    // std::string   _currentScenePath;
    std::unordered_map<entt::registry*, Scene*> _reg2scene;
    std::unordered_set<const Scene*>            _knownScenes;

  public:
    /**
      State:
      Engine Start->
      SceneManager created ->
      Open scene ->
      Scene initialized (onSceneInit) ->
      If viewport scene , onSceneActivated ->
      Engine running ->
      Close scene (onSceneDestroy) -> Unload scene -> Engine Quit
    */

    MulticastDelegate<void(Scene*)> onSceneInit;
    MulticastDelegate<void(Scene*)> onSceneDestroy;
    MulticastDelegate<void(Scene*)> onSceneActivated;

  public:

    SceneManager() = default;
    ~SceneManager();

    /**
     * @brief Load a scene from path
     * @param path The path to the scene file
     * @return true if loaded successfully, false otherwise
     */
    bool loadScene(const std::string& path);

    bool unloadScene();

    bool                 activateScene(stdptr<Scene> scene);
    bool                 destroyScene(stdptr<Scene>& scene);
    [[nodiscard]] Scene* getActiveScene() const { return _activeScene.get(); }
    [[nodiscard]] stdptr<Scene> getActiveSceneShared() const { return _activeScene; }
    bool                 hasScene() const { return _activeScene != nullptr; }


    void serializeToFile(const std::string& path, Scene* scene) const;
    void deserializeFromFile(const std::string& path, Scene* scene);

    bool isSceneValid(const Scene* ptr);
    void registerScenePointer(const Scene* ptr);
    void unregisterScenePointer(const Scene* ptr);

    stdptr<Scene> cloneScene(Scene* scene) const;

    Scene* getSceneByRegistry(entt::registry* reg)
    {
        if (!reg) {
            return nullptr;
        }
        auto it = _reg2scene.find(reg);
        if (it != _reg2scene.end()) {
            return it->second;
        }
        return nullptr;
    }

    /// @brief Check if we're in shutdown state (no scenes registered)
    bool isShuttingDown() const { return _reg2scene.empty() && !_activeScene; }

  private:
    void setActiveScene(stdptr<Scene> scene);
    void initSceneIfNeeded(Scene* scene);
    void destroySceneIfNeeded(stdptr<Scene>& scene);
    void onSceneInitInternal(Scene* scene);
    void onSceneDestroyInternal(Scene* scene);
    // void onSceneActivatedInternal(Scene *scene);
};

} // namespace ya
