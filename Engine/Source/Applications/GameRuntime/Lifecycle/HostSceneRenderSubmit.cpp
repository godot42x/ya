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
                .scene             = view.scene,
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

ExtractedSceneRender extractHostSceneSnapshots(SceneRenderPlan   plan,
                                               TerrainProcessor* terrainProcessor)
{
    return buildSceneSnapshots(std::move(plan), [terrainProcessor](Scene& scene)
    {
        return extractSceneSnapshot(scene, terrainProcessor);
    });
}

} // namespace ya
