#include "SceneManager.h"
#include "Core/Log.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Core/System/VirtualFileSystem.h"
#include "ECS/Component.h"


namespace ya
{

SceneManager::~SceneManager()
{
    // First cleanup _currentScene if it's different from _editorScene
    if (_currentScene && _currentScene != _editorScene) {
        onSceneDestroyInternal(_currentScene.get());
        _currentScene.reset();
    }
    
    // Then cleanup _editorScene
    if (_editorScene) {
        onSceneDestroyInternal(_editorScene.get());
        _editorScene.reset();
    }
    
    // Clear the mapping (should already be empty, but just in case)
    _reg2scene.clear();
}

bool SceneManager::loadScene(const std::string &path)
{
    // Unload existing scene first
    if (_currentScene) {
        // YA_CORE_WARN("Scene already loaded, unloading previous scene: {}", _currentScenePath);
        unloadScene();
    }

    // VirtualFileSystem::get()->getGameRoot()

    // Create new scene
    _editorScene = makeShared<Scene>();
    // _currentScenePath = path;
    SceneSerializer serializer(_editorScene.get());
    serializer.loadFromFile(path);

    onSceneInitInternal(_editorScene.get());
    // Call initialization callback if set
    setActiveScene(_editorScene);

    YA_CORE_INFO("Scene loaded: {}", path);
    return true;
}

bool SceneManager::unloadScene()
{
    // UNIMPLEMENTED();
    if (!_currentScene) {
        return false;
    }
    onSceneDestroyInternal(_currentScene.get());

    _currentScene.reset();
    // YA_CORE_INFO("Scene unloaded: {}", _currentScenePath);
    // _currentScenePath.clear();

    return true;
}

void SceneManager::setActiveScene(stdptr<Scene> scene)
{
    // Don't do anything if setting the same scene
    if (_currentScene == scene) {
        return;
    }
    
    // Cleanup old current scene if it's not the editor scene (runtime scene)
    if (_currentScene && _currentScene != _editorScene) {
        onSceneDestroyInternal(_currentScene.get());
    }
    
    _currentScene = scene;
    
    // Register new scene if not already registered (e.g., cloned scene)
    if (scene && !_reg2scene.contains(&scene->getRegistry())) {
        onSceneInitInternal(scene.get());
    }
    
    onSceneActivated.broadcast(scene.get());
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

void SceneManager::onStartRuntime()
{
    auto newScene = getEditorScene()->clone();
    // Note: setActiveScene will register the cloned scene to _reg2scene
    setActiveScene(newScene);
}

void SceneManager::onStopRuntime()
{
    // setActiveScene will cleanup the runtime scene and switch back to editor
    setActiveScene(_editorScene);
}

bool SceneManager::isSceneValid(const Scene *ptr)
{
    return ptr == _currentScene.get() || ptr == _editorScene.get();
}

stdptr<Scene> SceneManager::cloneScene(Scene *scene) const
{
    return scene->clone();
}

void SceneManager::onSceneInitInternal(Scene *scene)
{
    YA_CORE_ASSERT(!_reg2scene.contains(&scene->getRegistry()), "Scene registry already exists");
    _reg2scene[&scene->getRegistry()] = scene;

    onSceneInit.broadcast(scene);
}

void SceneManager::onSceneDestroyInternal(Scene *scene)
{
    if (!scene) {
        return;
    }
    
    // Broadcast destroy event first (while scene is still valid)
    onSceneDestroy.broadcast(scene);
    
    // Then remove from registry mapping
    auto it = _reg2scene.find(&scene->getRegistry());
    if (it != _reg2scene.end()) {
        _reg2scene.erase(it);
    }
}

} // namespace ya
