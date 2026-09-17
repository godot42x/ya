#include "GameRuntime/Lifecycle/HostSceneRenderSubmit.h"

#include "GameRuntime/Utility/RenderFrameExtractor.h"
#include "Scene/Core/Scene.h"

#include <functional>
#include <vector>

namespace ya
{

namespace
{

std::function<std::shared_ptr<const SceneFrameSnapshot>()> makeExtractBuilder(
    Scene* scene, TerrainProcessor* terrainProcessor)
{
    return [scene, terrainProcessor]
    {
        auto snapshot = std::make_shared<SceneFrameSnapshot>();
        RenderFrameExtractor::extractSceneSnapshot(
            RenderFrameExtractor::SceneExtractInput{
                .scene            = scene,
                .terrainProcessor = terrainProcessor,
            },
            *snapshot);
        return std::shared_ptr<const SceneFrameSnapshot>(std::move(snapshot));
    };
}

} // namespace

bool submitHostSceneViews(SceneRenderScheduler&                scheduler,
                          TerrainProcessor*                    terrainProcessor,
                          std::span<const HostSceneViewSubmit> views)
{
    if (!scheduler.isTickOpen()) {
        return false;
    }

    std::vector<Scene*> uniqueScenes;
    std::vector<std::function<std::shared_ptr<const SceneFrameSnapshot>()>> builders;
    uniqueScenes.reserve(views.size());
    builders.reserve(views.size());

    auto builderFor = [&](Scene* scene) -> std::function<std::shared_ptr<const SceneFrameSnapshot>()>
    {
        for (size_t index = 0; index < uniqueScenes.size(); ++index) {
            if (uniqueScenes[index] == scene) {
                return builders[index];
            }
        }
        uniqueScenes.push_back(scene);
        builders.push_back(makeExtractBuilder(scene, terrainProcessor));
        return builders.back();
    };

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
                .buildSnapshot     = builderFor(view.scene),
            })) {
            bSubmitted = true;
        }
    }
    return bSubmitted;
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
