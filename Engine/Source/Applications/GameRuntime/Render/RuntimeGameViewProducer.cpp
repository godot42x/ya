#include "GameRuntime/Render/RuntimeGameViewProducer.h"

#include "GameRuntime/App.h"
#include "GameRuntime/Render/SceneCameraQuery.h"

#include "ECS/Systems/Components/CameraComponent.h"
#include "Render3D/Common/RenderFeatures.h"
#include "Scene3D/TransformComponent.h"

namespace ya
{

void RuntimeGameViewProducer::collectSceneViews(const SceneViewCollectContext& context,
                                               SceneViewCollector&            collector)
{
    if (!_app || !context.activeScene || !_app->isRuntimeMode()) {
        return;
    }

    SceneViewDesc primary{
        .scene        = context.activeScene,
        .viewId       = displayRootKey().viewId(),
        // Fills the host viewport, so its offscreen rect is the host's render
        // resolution. The window is not consulted: how this image is presented is
        // the presentation pass's business.
        .outputRect = Rect2D{.pos = {0.0f, 0.0f}, .extent = context.renderResolution.toVec2()},
        // Generated editor companions are editor furniture and this is the game
        // view, so it draws authored content, always. "Show Editor Gizmos" is
        // the editor's view option; it never reaches a view the editor does not
        // declare.
        .features = toMask(ERenderFeature::Game),
    };

    Entity* camera = findPrimaryCamera(*context.activeScene);
    if (camera && camera->isValid() && camera->hasComponent<CameraComponent>()) {
        auto* cameraComponent    = camera->getComponent<CameraComponent>();
        auto* transformComponent = camera->getComponent<TransformComponent>();
        primary.view       = cameraComponent->getFreeView();
        primary.projection = cameraComponent->getProjection();
        primary.cameraPos  = transformComponent ? transformComponent->getWorldPosition() : glm::vec3(0.0f);
        // The view is rendered from that camera, so that camera's own generated
        // body is not part of what it sees.
        primary.viewOwner = camera->getHandle();
    }

    collector.declare(primary);
}

} // namespace ya
