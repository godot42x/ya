#include "SceneManager.h"

#include "Core/Log.h"
#include "Render3D/SceneSerializer.h"

namespace ya
{

SceneManager::~SceneManager()
{
    destroySceneIfNeeded(_activeScene);

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

    initSceneIfNeeded(scene.get());
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

bool SceneManager::isSceneValid(const Scene* ptr)
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

void SceneManager::initSceneIfNeeded(Scene* scene)
{
    if (!scene || _reg2scene.contains(&scene->getRegistry())) {
        return;
    }

    onSceneInitInternal(scene);
}

void SceneManager::destroySceneIfNeeded(stdptr<Scene>& scene)
{
    if (!scene) {
        return;
    }

    if (_activeScene == scene) {
        _activeScene.reset();
    }

    onSceneDestroyInternal(scene.get());
    scene.reset();
}

void SceneManager::onSceneInitInternal(Scene* scene)
{
    YA_CORE_ASSERT(scene, "SceneManager::onSceneInitInternal got null scene");
    YA_CORE_ASSERT(!_reg2scene.contains(&scene->getRegistry()), "Scene registry already exists");
    _reg2scene[&scene->getRegistry()] = scene;
    onSceneInit.broadcast(scene);
}

void SceneManager::onSceneDestroyInternal(Scene* scene)
{
    if (!scene) {
        return;
    }

    onSceneDestroy.broadcast(scene);

    auto it = _reg2scene.find(&scene->getRegistry());
    if (it != _reg2scene.end()) {
        _reg2scene.erase(it);
    }
}

} // namespace ya
