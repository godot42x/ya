#include "GameEditor/EditorPlaySession.h"

#include "Core/Log.h"
#include "GameRuntime/App.h"
#include "Scene/Runtime/SceneManager.h"

namespace ya
{

bool EditorPlaySession::begin(App& app, AppState nextState)
{
    auto* sceneManager = app.getSceneServices().getSceneManager();
    if (!sceneManager) {
        return false;
    }

    _authoringScene = sceneManager->getActiveSceneShared();
    if (!_authoringScene) {
        YA_CORE_WARN("Cannot begin editor play session without an active scene");
        return false;
    }

    _playScene = sceneManager->cloneScene(_authoringScene.get());
    if (!_playScene) {
        YA_CORE_ERROR("Failed to clone scene for editor play session");
        return false;
    }

    _playScene->setName(_authoringScene->getName() + " (Play Mode)");
    sceneManager->activateScene(_playScene);
    (void)nextState;
    return true;
}

void EditorPlaySession::end(App& app)
{
    if (!_playScene) {
        return;
    }

    if (auto* sceneManager = app.getSceneServices().getSceneManager()) {
        if (_authoringScene) {
            sceneManager->activateScene(_authoringScene);
        }
        sceneManager->destroyScene(_playScene);
    }
    else {
        _playScene.reset();
    }
}

void EditorPlaySession::shutdown(App& app)
{
    end(app);
    _authoringScene.reset();
}

void EditorPlaySession::onSceneActivated(App& app, Scene* scene)
{
    if (scene && _playScene && scene == _playScene.get()) {
        return;
    }

    if (app.isStopped()) {
        if (auto* sceneManager = app.getSceneServices().getSceneManager()) {
            _authoringScene = sceneManager->getActiveSceneShared();
        }
    }
}

void EditorPlaySession::onSceneDestroyed(Scene* scene)
{
    if (!scene) {
        return;
    }
    if (_authoringScene.get() == scene) {
        _authoringScene.reset();
    }
    if (_playScene.get() == scene) {
        _playScene.reset();
    }
}

} // namespace ya
