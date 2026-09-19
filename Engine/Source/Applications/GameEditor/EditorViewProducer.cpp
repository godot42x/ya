#include "GameEditor/EditorViewProducer.h"

#include "GameEditor/EditorLayer.h"
#include "GameRuntime/App.h"
#include "GameRuntime/AppRenderServices.h"

#include "Core/Camera/Camera.h"
#include "Core/Math/Math.h"
#include "ECS/Systems/Components/CameraComponent.h"
#include "Render3D/Common/ViewCompose.h"
#include "Scene3D/TransformComponent.h"

namespace ya
{

namespace
{

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

Rect2D EditorViewProducer::previewRect(const Rect2D& authoringRect)
{
    return makeViewDisplayInsetRect(authoringRect.extent);
}

void EditorViewProducer::collectSceneViews(const SceneViewCollectContext& context,
                                          SceneViewCollector&            collector)
{
    if (!_app || !_layer || !context.activeScene) {
        return;
    }

    // No viewport on screen means declaring nothing at all. The viewport is a
    // docked tab and the dock detaches its widget when another tab in the stack
    // is selected, so this is the ordinary "the user is looking at the Inspector"
    // case, not an error state. Gating here rather than making the passes skip
    // their work keeps the decision with the View's owner: no View means no
    // family, no targets, no graph.
    if (!_layer->isViewportShown()) {
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

    // The authoring panel's geometry is the editor's own fact: the editor
    // declares it rather than pushing it into host state and reading it back.
    const Rect2D authoringRect = _layer->getViewportRect();

    // While the game runs, its own producer owns the world viewport: the editor
    // camera is not what that viewport shows, so only the preview is ours.
    if (!_app->isRuntimeMode()) {
        const FreeCamera& editorCamera = _layer->getCamera();
        collector.declare(SceneViewDesc{
            .scene        = context.activeScene,
            .viewId       = authoringKey().viewId(),
            .view         = editorCamera.getViewMatrix(),
            .projection   = editorCamera.getProjectionMatrix(),
            .cameraPos    = editorCamera.getPosition(),
            .viewportRect = authoringRect,
            .features     = baseFeatures | (bEditorGizmos ? gizmoFeature : 0u),
        });
    }

    // The preview shows the camera the user selected. Which camera that is stays
    // the editor's choice.
    Entity* previewCamera = _layer->getCameraPreviewEntity();
    if (!previewCamera) {
        return;
    }
    auto* cameraComponent    = previewCamera->getComponent<CameraComponent>();
    auto* transformComponent = previewCamera->getComponent<TransformComponent>();
    if (!cameraComponent || !transformComponent) {
        return;
    }

    const Rect2D previewRect = EditorViewProducer::previewRect(authoringRect);
    if (previewRect.extent.x <= 0.0f || previewRect.extent.y <= 0.0f) {
        return;
    }
    const Rect2D previewOutput{
        .pos    = {0.0f, 0.0f},
        .extent = previewRect.extent,
    };
    collector.declare(SceneViewDesc{
        .scene             = context.activeScene,
        .viewId            = previewKey().viewId(),
        .view              = cameraComponent->getFreeView(),
        .projection        = cameraProjectionForOutput(*cameraComponent, previewOutput.extent),
        .cameraPos         = transformComponent->getWorldPosition(),
        .viewportRect      = previewOutput,
        // The preview composes onto the authoring viewport of the *same* owner:
        // the key names that owner rather than a global "primary" id, so a
        // preview can only ever land on a View this producer declared.
        .composeOntoViewId = authoringKey().viewId(),
        // No inset rect: the runtime must not blit this View onto the world
        // render target. The preview is viewport chrome, and chrome is composed
        // by the GUI after the world image, so the world overlays (grid,
        // manipulator, frustum wireframe) stay under it by construction instead
        // of by recording order. Declaring the View as non-display-root but
        // without an inset is exactly "rendered into its own image, shown by
        // whoever asked for it".
        .composeRect       = {},
        // What that camera sees, and only that: a preview is not an authoring
        // view, so it draws no generated editor companions.
        .features          = baseFeatures,
        .viewOwner         = previewCamera->getHandle(),
    });
}

} // namespace ya
