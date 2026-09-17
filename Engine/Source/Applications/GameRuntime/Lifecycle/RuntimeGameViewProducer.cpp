#include "GameRuntime/Lifecycle/RuntimeGameViewProducer.h"

#include "GameRuntime/App.h"
#include "GameRuntime/Utility/SceneCameraQuery.h"

#include "ECS/Systems/Components/CameraComponent.h"
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
        .viewId       = kPrimarySceneViewId,
        .viewportRect = context.viewportRect,
    };

    Entity* camera = findPrimaryCamera(*context.activeScene);
    if (camera && camera->isValid() && camera->hasComponent<CameraComponent>()) {
        auto* cameraComponent    = camera->getComponent<CameraComponent>();
        auto* transformComponent = camera->getComponent<TransformComponent>();
        primary.view       = cameraComponent->getFreeView();
        primary.projection = cameraComponent->getProjection();
        primary.cameraPos  = transformComponent ? transformComponent->getWorldPosition() : glm::vec3(0.0f);
    }

    collector.declare(primary);
}

} // namespace ya
