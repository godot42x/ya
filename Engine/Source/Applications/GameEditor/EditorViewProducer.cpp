#include "GameEditor/EditorViewProducer.h"

#include "GameEditor/EditorLayer.h"
#include "GameRuntime/App.h"

#include "Core/Camera/Camera.h"
#include "Core/Math/Math.h"
#include "ECS/Systems/Components/CameraComponent.h"
#include "Render3D/Common/ViewCompose.h"
#include "Scene3D/TransformComponent.h"

namespace ya
{

namespace
{

/// View identity of the editor's camera preview inset. Editor-owned: the host no
/// longer mints view ids for views it does not show.
constexpr SceneViewId kEditorPreviewViewId = 2;

/// The preview renders into its own small RT, so its projection follows that
/// rect's aspect unless the camera pins its own.
glm::mat4 cameraProjectionForOutput(const CameraComponent& camera, const glm::vec2& outputExtent)
{
    if (camera._fixedAspectRatio || outputExtent.x <= 0.0f || outputExtent.y <= 0.0f) {
        return camera.getProjection();
    }
    return FMath::perspective(glm::radians(camera._fov),
                              outputExtent.x / outputExtent.y,
                              camera._nearClip,
                              camera._farClip);
}

} // namespace

void EditorViewProducer::collectSceneViews(const SceneViewCollectContext& context,
                                          SceneViewCollector&            collector)
{
    if (!_app || !_layer || !context.activeScene) {
        return;
    }

    // The 2D canvas workspace draws no world view at all, so it has neither a
    // primary view nor a preview inset. That is a declaration the editor makes
    // here rather than a switch the runtime has to honour.
    if (_layer->isViewportMode2D()) {
        return;
    }

    // Generated editor companions are editor furniture: the authoring view draws
    // them while the app is stopped and on request afterwards, a camera preview
    // shows what that camera sees, and the game view never draws them. The
    // request itself is the editor's own view option, so it is read from the
    // editor rather than from app-wide render state.
    const FRenderFeatureMask gizmoFeature = toMask(ERenderFeature::Gizmo);
    const FRenderFeatureMask baseFeatures = toMask(ERenderFeature::Game);
    const bool               bEditorGizmos = _app->isStopped() || _layer->isEditorGizmoShown();

    // While the game runs, its own producer owns the world viewport: the editor
    // camera is not what that viewport shows, so only the preview is ours.
    if (!_app->isRuntimeMode()) {
        const FreeCamera& editorCamera = _layer->getCamera();
        collector.declare(SceneViewDesc{
            .scene        = context.activeScene,
            .viewId       = kPrimarySceneViewId,
            .view         = editorCamera.getViewMatrix(),
            .projection   = editorCamera.getProjectionMatrix(),
            .cameraPos    = editorCamera.getPosition(),
            .viewportRect = context.viewportRect,
            .features     = baseFeatures | (bEditorGizmos ? gizmoFeature : 0u),
        });
    }

    // The preview inset shows the camera the user selected, composed onto the
    // primary view's display. Which camera that is stays the editor's choice.
    Entity* previewCamera = _layer->getCameraPreviewEntity();
    if (!previewCamera) {
        return;
    }
    auto* cameraComponent    = previewCamera->getComponent<CameraComponent>();
    auto* transformComponent = previewCamera->getComponent<TransformComponent>();
    if (!cameraComponent || !transformComponent) {
        return;
    }

    const Rect2D composeRect = makeViewDisplayInsetRect(context.viewportRect.extent);
    if (composeRect.extent.x <= 0.0f || composeRect.extent.y <= 0.0f) {
        return;
    }
    const Rect2D previewOutput{
        .pos    = {0.0f, 0.0f},
        .extent = composeRect.extent,
    };
    collector.declare(SceneViewDesc{
        .scene             = context.activeScene,
        .viewId            = kEditorPreviewViewId,
        .view              = cameraComponent->getFreeView(),
        .projection        = cameraProjectionForOutput(*cameraComponent, previewOutput.extent),
        .cameraPos         = transformComponent->getWorldPosition(),
        .viewportRect      = previewOutput,
        .composeOntoViewId = kPrimarySceneViewId,
        .composeRect       = composeRect,
        .features          = baseFeatures | (_layer->isEditorGizmoShown() ? gizmoFeature : 0u),
        .viewOwner         = previewCamera->getHandle(),
    });
}

} // namespace ya
