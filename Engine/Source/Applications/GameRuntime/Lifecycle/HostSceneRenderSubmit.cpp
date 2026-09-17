#include "GameRuntime/Lifecycle/HostSceneRenderSubmit.h"

#include "GameRuntime/Utility/RenderFrameExtractor.h"
#include "Scene/Core/Scene.h"

namespace ya
{

namespace
{

std::shared_ptr<const SceneSnapshot> extractSceneSnapshot(Scene& scene, TerrainProcessor* terrainProcessor)
{
    auto snapshot = std::make_shared<SceneSnapshot>();
    RenderFrameExtractor::extractSceneSnapshot(
        RenderFrameExtractor::SceneExtractInput{
            .scene            = &scene,
            .terrainProcessor = terrainProcessor,
        },
        *snapshot);
    return std::shared_ptr<const SceneSnapshot>(std::move(snapshot));
}

} // namespace

bool submitHostSceneViews(SceneRenderScheduler&                scheduler,
                          std::span<const HostSceneViewSubmit> views)
{
    if (!scheduler.isTickOpen()) {
        return false;
    }

    bool bSubmitted = views.empty();
    for (const HostSceneViewSubmit& view : views) {
        if (!view.scene) {
            continue;
        }
        if (scheduler.submit(SceneRenderRequest{
                .sceneId           = view.scene->getInstanceId(),
                .sceneRevision     = view.sceneRevision,
                .viewId            = view.viewId,
                .familyId          = view.familyId,
                .view              = view.view,
                .projection        = view.projection,
                .viewProjection    = makeCameraViewProjection(view.projection, view.view),
                .cameraPos         = view.cameraPos,
                .viewportRect      = view.viewportRect,
                .composeOntoViewId = view.composeOntoViewId,
                .composeRect       = view.composeRect,
            })) {
            bSubmitted = true;
        }
    }
    return bSubmitted;
}

uint32_t extractHostSceneSnapshots(SceneRenderPlan&                     plan,
                                   std::span<const HostSceneViewSubmit> views,
                                   TerrainProcessor*                    terrainProcessor)
{
    return buildSceneSnapshots(plan, [&views, terrainProcessor](SceneId sceneId, uint64_t sceneRevision)
    {
        for (const HostSceneViewSubmit& view : views) {
            if (!view.scene || view.sceneRevision != sceneRevision ||
                view.scene->getInstanceId() != sceneId) {
                continue;
            }
            return extractSceneSnapshot(*view.scene, terrainProcessor);
        }
        return std::shared_ptr<const SceneSnapshot>{};
    });
}

Scene* derivedSceneForHostView(std::span<const HostSceneViewSubmit> views,
                               const SceneViewportTask&             task)
{
    Scene* bySceneId = nullptr;
    for (const HostSceneViewSubmit& view : views) {
        if (!view.scene || view.scene->getInstanceId() != task.sceneId) {
            continue;
        }
        if (view.viewId == task.viewId) {
            return view.scene;
        }
        if (!bySceneId) {
            bySceneId = view.scene;
        }
    }
    return bySceneId;
}

} // namespace ya
