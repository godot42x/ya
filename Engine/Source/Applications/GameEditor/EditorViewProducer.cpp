#include "GameEditor/EditorViewProducer.h"

#include "GameEditor/EditorLayer.h"
#include "GameRuntime/App.h"
#include "GameRuntime/AppRenderServices.h"
#include "GameRuntime/Render/SceneCameraQuery.h"

#include "Core/Camera/Camera.h"
#include "ECS/Systems/Components/CameraComponent.h"
#include "Scene3D/TransformComponent.h"

namespace
{

} // namespace

namespace ya
{

namespace
{
/// Place a dest rect in the host display RT (origin at the RT top-left).
/// Editor layout policy for the camera preview inside the authoring panel;
/// moved out of Render3D, which never knew what a preview was.
[[nodiscard]] Rect2D makeViewDisplayInsetRect(const glm::vec2& hostExtent,
                                              float            widthFraction  = 0.22f,
                                              float            marginFraction = 0.02f)
{
    if (hostExtent.x <= 1.0f || hostExtent.y <= 1.0f) {
        return {};
    }

    const float safeWidthFraction  = std::clamp(widthFraction, 0.05f, 0.5f);
    const float safeMarginFraction = std::clamp(marginFraction, 0.2f, 0.2f);
    const float margin             = std::max(hostExtent.x, hostExtent.y) * safeMarginFraction;
    float       width              = hostExtent.x * safeWidthFraction;
    float       height             = width * (hostExtent.y / hostExtent.x);
    if (height + margin * 2.0f > hostExtent.y) {
        height = std::max(1.0f, hostExtent.y - margin * 2.0f);
        width  = height * (hostExtent.x / hostExtent.y);
    }
    width  = std::max(1.0f, std::min(width, hostExtent.x - margin));
    height = std::max(1.0f, std::min(height, hostExtent.y - margin));

    return Rect2D{
        .pos    = {hostExtent.x - margin - width, hostExtent.y - margin - height},
        .extent = {width, height},
    };
}

/// The preview renders into its own small RT, so its projection follows that
/// rect's aspect unless the camera pins its own.
glm::mat4 cameraProjectionForOutput(const CameraComponent& camera, const glm::vec2& outputExtent)
{
    const float outputAspect = (outputExtent.x > 0.0f && outputExtent.y > 0.0f)
        ? outputExtent.x / outputExtent.y
        : camera._aspectRatio;
    return camera.getProjection(outputAspect);
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
            .outputRect = authoringRect,
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
        .view              = cameraView(*previewCamera),
        .projection        = cameraProjectionForOutput(*cameraComponent, previewOutput.extent),
        .cameraPos         = transformComponent->getWorldPosition(),
        .outputRect      = previewOutput,
        // The preview is material, not the display: it renders into its own
        // image, and the viewport chrome samples it. Declaring it
        // non-display-root is what keeps the authoring viewport (declared by
        // the same producer) as the display root.
        .bDisplayRoot    = false,
        // What that camera sees, and only that: a preview is not an authoring
        // view, so it draws no generated editor companions.
        .features          = baseFeatures,
        .viewOwner         = previewCamera->getHandle(),
    });
}

} // namespace ya
