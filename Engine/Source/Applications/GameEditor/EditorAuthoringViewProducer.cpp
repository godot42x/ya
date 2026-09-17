#include "GameEditor/EditorAuthoringViewProducer.h"

#include "GameEditor/EditorLayer.h"
#include "GameRuntime/App.h"

#include "Core/Camera/Camera.h"

namespace ya
{

void EditorAuthoringViewProducer::collectSceneViews(const SceneViewCollectContext& context,
                                                   SceneViewCollector&            collector)
{
    if (!_app || !_layer || !context.activeScene) {
        return;
    }

    // While the game runs, its own producer owns the world viewport: the editor
    // camera is not what that viewport shows.
    if (_app->isRuntimeMode()) {
        return;
    }

    // The 2D canvas workspace draws no world view at all. That is a declaration
    // the editor makes here rather than a switch the runtime has to honour.
    if (_layer->isViewportMode2D()) {
        return;
    }

    const FreeCamera& editorCamera = _layer->getCamera();
    collector.declare(SceneViewDesc{
        .scene        = context.activeScene,
        .viewId       = kPrimarySceneViewId,
        .view         = editorCamera.getViewMatrix(),
        .projection   = editorCamera.getProjectionMatrix(),
        .cameraPos    = editorCamera.getPosition(),
        .viewportRect = context.viewportRect,
    });
}

} // namespace ya
