#pragma once

#include "Core/Base.h"
#include "Core/Delegate.h"
#include "Scene/Core/ISceneLifecycleHost.h"
#include "Scene/Core/Scene.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
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
struct YA_SCENE_RUNTIME_API SceneManager : public ISceneLifecycleHost
{
  public:
    using SceneInitCallback = std::function<void(Scene*)>;

  private:
    /// An initialized scene the manager does not own. `lifetime` keeps the
    /// destroy broadcast from running the destructor mid-fanout; it expires
    /// once the last owner drops, which is exactly when ~Scene runs.
    struct FInitializedScene
    {
        Scene*               scene = nullptr;
        std::weak_ptr<Scene> lifetime;
    };

    stdptr<Scene>                                   _activeScene = nullptr;
    std::unordered_map<entt::registry*, FInitializedScene> _reg2scene;
    std::unordered_set<const Scene*>                _knownScenes;
    /// Scenes whose onSceneDestroy broadcast is on the stack. Re-entry from
    /// ~Scene must not broadcast a second time.
    std::unordered_set<Scene*>                      _scenesAnnouncingDestroy;

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


    bool serializeToFile(const std::string& path, Scene* scene) const;
    bool deserializeFromFile(const std::string& path, Scene* scene);

    bool isSceneValid(const Scene* ptr) const override;
    void registerScenePointer(const Scene* ptr) override;
    void unregisterScenePointer(const Scene* ptr) override;
    void notifySceneDestructing(Scene* scene) override;

    stdptr<Scene> cloneScene(Scene* scene) const;

    Scene* getSceneByRegistry(entt::registry* reg);

    /// @brief Check if we're in shutdown state (no scenes registered)
    bool isShuttingDown() const { return _reg2scene.empty() && !_activeScene; }

  private:
    void setActiveScene(stdptr<Scene> scene);
    void initSceneIfNeeded(const stdptr<Scene>& scene);
    void destroySceneIfNeeded(stdptr<Scene>& scene);
    /// `bAlways`: explicit destroyScene also notifies a scene that never
    /// received onSceneInit. The destructor path passes false.
    void announceSceneDestroy(Scene* scene, bool bAlways);
    // void onSceneActivatedInternal(Scene *scene);
};

} // namespace ya
