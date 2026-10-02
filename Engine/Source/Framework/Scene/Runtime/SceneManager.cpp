#include "Scene/Runtime/SceneManager.h"

#include "Core/Log.h"
#include "Scene/Serialization/SceneSerializer.h"

namespace ya
{

SceneManager::~SceneManager()
{
    // Every scene that received onSceneInit gets onSceneDestroy while it is
    // still alive, including scenes that are no longer active. Tests (and a
    // manager torn down before its scenes) rely on this running even when
    // ~Scene later sees a null lifecycle host.
    while (!_reg2scene.empty()) {
        const FInitializedScene record = _reg2scene.begin()->second;
        if (record.lifetime.expired() || record.scene == nullptr) {
            _reg2scene.erase(_reg2scene.begin());
            continue;
        }
        announceSceneDestroy(record.scene, false);
        if (!_reg2scene.empty() && _reg2scene.begin()->second.scene == record.scene) {
            _reg2scene.erase(_reg2scene.begin());
        }
    }

    _activeScene.reset();

    if (Scene::getLifecycleHost() == this) {
        Scene::setLifecycleHost(nullptr);
    }
    _reg2scene.clear();
    _knownScenes.clear();
}

void SceneManager::registerScenePointer(const Scene* ptr)
{
    if (!ptr) {
        return;
    }
    _knownScenes.insert(ptr);
}

void SceneManager::unregisterScenePointer(const Scene* ptr)
{
    if (!ptr) {
        return;
    }
    _knownScenes.erase(ptr);
}

void SceneManager::notifySceneDestructing(Scene* scene)
{
    announceSceneDestroy(scene, false);
}

Scene* SceneManager::getSceneByRegistry(entt::registry* reg)
{
    if (!reg) {
        return nullptr;
    }
    const auto it = _reg2scene.find(reg);
    if (it == _reg2scene.end()) {
        return nullptr;
    }
    if (it->second.lifetime.expired()) {
        _reg2scene.erase(it);
        return nullptr;
    }
    return it->second.scene;
}

bool SceneManager::loadScene(const std::string& path)
{
    unloadScene();

    auto nextScene = makeShared<Scene>();
    SceneSerializer serializer(nextScene.get());
    if (!serializer.loadFromFile(path)) {
        YA_CORE_ERROR("Failed to load scene: {}, falling back to an empty scene", path);
        nextScene->setName("Untitled Scene");
        return activateScene(nextScene);
    }

    return activateScene(nextScene);
}

bool SceneManager::unloadScene()
{
    return destroyScene(_activeScene);
}

bool SceneManager::activateScene(stdptr<Scene> scene)
{
    if (_activeScene == scene) {
        return true;
    }

    initSceneIfNeeded(scene);
    setActiveScene(std::move(scene));
    return true;
}

bool SceneManager::destroyScene(stdptr<Scene>& scene)
{
    if (!scene) {
        return false;
    }

    destroySceneIfNeeded(scene);
    return true;
}

bool SceneManager::isSceneValid(const Scene* ptr) const
{
    return ptr && _knownScenes.contains(ptr);
}

stdptr<Scene> SceneManager::cloneScene(Scene* scene) const
{
    return scene ? scene->clone() : nullptr;
}

bool SceneManager::serializeToFile(const std::string& path, Scene* scene) const
{
    if (!scene) {
        YA_CORE_WARN("No scene loaded to serialize");
        return false;
    }

    SceneSerializer serializer(scene);
    if (serializer.saveToFile(path)) {
        YA_CORE_INFO("Scene serialized to file: {}", path);
        return true;
    }
    YA_CORE_ERROR("Failed to serialize scene to file: {}", path);
    return false;
}

bool SceneManager::deserializeFromFile(const std::string& path, Scene* scene)
{
    if (!scene) {
        YA_CORE_WARN("No scene provided to deserialize into");
        return false;
    }

    SceneSerializer serializer(scene);
    if (serializer.loadFromFile(path)) {
        YA_CORE_INFO("Scene deserialized from file: {}", path);
        return true;
    }
    YA_CORE_ERROR("Failed to deserialize scene from file: {}", path);
    return false;
}

void SceneManager::setActiveScene(stdptr<Scene> scene)
{
    if (_activeScene == scene) {
        return;
    }

    _activeScene = scene;
    onSceneActivated.broadcast(_activeScene.get());
}

void SceneManager::initSceneIfNeeded(const stdptr<Scene>& scene)
{
    if (!scene) {
        return;
    }

    const auto it = _reg2scene.find(&scene->getRegistry());
    if (it != _reg2scene.end()) {
        if (!it->second.lifetime.expired() && it->second.scene == scene.get()) {
            return;
        }
        _reg2scene.erase(it);
    }

    _reg2scene.emplace(&scene->getRegistry(), FInitializedScene{scene.get(), scene});
    onSceneInit.broadcast(scene.get());
}

void SceneManager::destroySceneIfNeeded(stdptr<Scene>& scene)
{
    if (!scene) {
        return;
    }

    // The destroy contract: the Scene stays alive for the WHOLE onSceneDestroy
    // broadcast. `scene` may be the caller's last reference (e.g. the editor
    // play session), and a listener may drop it while the broadcast is still
    // being delivered (EditorPlaySession clears its play scene from
    // onSceneDestroyed). Without this keep-alive the Scene would be destroyed
    // mid-broadcast and later listeners (linkage rules disconnecting entt
    // signals) would dereference a freed Scene/registry.
    // announceSceneDestroy also locks the weak lifetime recorded at init, so
    // the same guarantee holds when the only remaining owner is not `scene`.
    const stdptr<Scene> keepAlive = scene;

    // Notify lifecycle listeners BEFORE releasing the last reference. When
    // `scene` aliases `_activeScene` (unloadScene path), resetting it first
    // would destroy the Scene object and leave the broadcast with a null
    // pointer, silently skipping onSceneDestroy.
    announceSceneDestroy(scene.get(), true);
    if (_activeScene == scene) {
        _activeScene.reset();
    }
    scene.reset();
    // keepAlive drops here, after every listener has run.
}

void SceneManager::announceSceneDestroy(Scene* scene, bool bAlways)
{
    if (!scene || _scenesAnnouncingDestroy.contains(scene)) {
        return;
    }

    // Drop the initialized-scene entry BEFORE the broadcast. ~Scene re-enters
    // this function (notifySceneDestructing) if a listener drops the last
    // owner, and must observe "already announced". The weak lock below is
    // what keeps that from happening until the broadcast returns; during
    // ~Scene itself the weak has already expired and the registry is still
    // alive until ~Scene calls clear().
    bool          bInitialized = false;
    stdptr<Scene> keepAlive;
    const auto    it = _reg2scene.find(&scene->getRegistry());
    if (it != _reg2scene.end()) {
        if (it->second.scene == scene) {
            bInitialized = true;
            keepAlive    = it->second.lifetime.lock();
        }
        _reg2scene.erase(it);
    }
    if (!bInitialized && !bAlways) {
        return;
    }

    _scenesAnnouncingDestroy.insert(scene);
    onSceneDestroy.broadcast(scene);
    _scenesAnnouncingDestroy.erase(scene);
}

} // namespace ya
