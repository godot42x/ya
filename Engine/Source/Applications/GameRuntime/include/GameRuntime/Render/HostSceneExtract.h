#pragma once

#include "Core/Base.h"
#include "Render3D/Common/SceneRenderScheduler.h"

namespace ya
{

class TerrainProcessor;

/// The host half of the explicit extraction step: resolve a sealed plan's
/// snapshot table, one extract per declared Scene, using the host's own Scene
/// reader (ECS traversal plus terrain). Scenes the host has no content for are
/// dropped from the plan by `buildSceneSnapshots`.
///
/// The returned value pairs the surviving Views with their plan entries; the
/// host fills their frame data in with `pairViewFrames()`.
[[nodiscard]] YA_GAME_RUNTIME_API ExtractedSceneRender extractHostSceneSnapshots(
    SceneRenderPlan   plan,
    TerrainProcessor* terrainProcessor);

} // namespace ya
