#include "SceneManager.h"

#include "Core/Log.h"
#include "Core/Serialization/SceneSerializer.h"

namespace ya
{

SceneManager::~SceneManager()
{
    destroySceneIfNeeded(_playScene);
    destroySceneIfNeeded(_editorScene);
    _activeScene.reset();

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
    if (isInPlayMode()) {
        YA_CORE_WARN("SceneManager::loadScene called while play mode is active, exiting play mode first");
        exitPlayMode();
    }

    auto nextScene = makeShared<Scene>();
    SceneSerializer serializer(nextScene.get());
    if (!serializer.loadFromFile(path)) {
        YA_CORE_ERROR("Failed to load scene: {}, falling back to an empty scene", path);
        nextScene->setName("Untitled Scene");
        return setEditorScene(nextScene);
    }

    return setEditorScene(nextScene);
}

bool SceneManager::unloadScene()
{
    if (isInPlayMode()) {
        exitPlayMode();
    }

    const bool bHadScene = (_editorScene != nullptr);
    destroySceneIfNeeded(_editorScene);
    _appState = AppState::Editor;
    _activeScene.reset();
    return bHadScene;
}

bool SceneManager::setEditorScene(stdptr<Scene> scene)
{
    if (isInPlayMode()) {
        YA_CORE_WARN("Cannot replace editor scene while play mode is active");
        return false;
    }

    if (_editorScene == scene) {
        setActiveScene(_editorScene);
        return true;
    }

    destroySceneIfNeeded(_editorScene);
    _editorScene = scene;
    initSceneIfNeeded(_editorScene.get());
    _appState = AppState::Editor;
    setActiveScene(_editorScene);
    return true;
}

bool SceneManager::enterPlayMode(AppState state)
{
    YA_CORE_ASSERT(state != AppState::Editor, "enterPlayMode requires a play state");

    if (!_editorScene) {
        YA_CORE_WARN("Cannot enter play mode without an editor scene");
        return false;
    }

    if (isInPlayMode()) {
        if (_appState == state) {
            return true;
        }
        exitPlayMode();
    }

    auto playScene = _editorScene->clone();
    playScene->setName(_editorScene->getName() + " (Play Mode)");
    if (!playScene) {
        YA_CORE_ERROR("Failed to clone editor scene for play mode");
        return false;
    }

    _appState = state;
    _playScene = std::move(playScene);
    initSceneIfNeeded(_playScene.get());
    setActiveScene(_playScene);
    return true;
}

bool SceneManager::exitPlayMode()
{
    if (!isInPlayMode()) {
        setActiveScene(_editorScene);
        _appState = AppState::Editor;
        return false;
    }

    destroySceneIfNeeded(_playScene);
    _appState = AppState::Editor;
    setActiveScene(_editorScene);
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

void SceneManager::serializeToFile(const std::string& path, Scene* scene) const
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

void SceneManager::deserializeFromFile(const std::string& path, Scene* scene)
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
