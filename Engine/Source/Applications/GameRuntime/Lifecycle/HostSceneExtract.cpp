#include "GameRuntime/Lifecycle/HostSceneExtract.h"

#include "GameRuntime/Lifecycle/RenderFrameExtractor.h"
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

ExtractedSceneRender extractHostSceneSnapshots(SceneRenderPlan   plan,
                                               TerrainProcessor* terrainProcessor)
{
    return buildSceneSnapshots(std::move(plan), [terrainProcessor](Scene& scene)
    {
        return extractSceneSnapshot(scene, terrainProcessor);
    });
}

} // namespace ya
