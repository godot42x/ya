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
    if (!scene) {
        return;
    }

    // begin() activates the cloned play scene while the app is still Stopped
    // (the state flip happens after onBeforeAppStateChange). That activation
    // is already recorded in _playScene and must not replace the authoring scene.
    if (_playScene && scene == _playScene.get()) {
        return;
    }

    auto* sceneManager = app.getSceneServices().getSceneManager();
    if (!sceneManager) {
        return;
    }

    if (app.isStopped()) {
        _authoringScene = sceneManager->getActiveSceneShared();
        return;
    }

    // Play is running. A scene activated on the play line (world.loadScene
    // transfer) becomes the session's play scene, so end() restores authoring
    // and destroys it. The authoring scene itself is what end() activates.
    if (_authoringScene && scene != _authoringScene.get() && sceneManager->getActiveScene() == scene) {
        _playScene = sceneManager->getActiveSceneShared();
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
