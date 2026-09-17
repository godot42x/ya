#pragma once

#include "Core/Base.h"
#include "Render3D/Common/RenderFrameInputs.h"
#include "Render3D/Common/SceneRenderScheduler.h"

#include <span>

namespace ya
{

struct Scene;
class TerrainProcessor;

/// One live Scene viewport the host wants recorded this frame. Scheduler never
/// owns the Scene; `scene` must stay alive until the sealed plan's snapshots
/// have been consumed by prepareView / record.
struct HostSceneViewSubmit
{
    Scene*      scene            = nullptr;
    SceneViewId viewId           = kPrimarySceneViewId;
    uint64_t    sceneRevision    = 0;
    uint64_t    familyId         = 1;
    glm::mat4   view             = glm::mat4(1.0f);
    glm::mat4   projection       = glm::mat4(1.0f);
    glm::vec3   cameraPos        = glm::vec3(0.0f);
    Rect2D      viewportRect     = {};
    SceneViewId composeOntoViewId = 0;
    Rect2D      composeRect      = {};
};

/// Declare every live Scene viewport for this tick. Same Scene* lands in one
/// snapshot table entry; different Scenes produce different entries and
/// families. Null `scene` entries are skipped. The scheduler tick must already
/// be open. Declarations only - no Scene/ECS content is read here.
[[nodiscard]] YA_GAME_RUNTIME_API bool submitHostSceneViews(SceneRenderScheduler&                scheduler,
                                                            std::span<const HostSceneViewSubmit> views);

/// Explicit extraction step for a sealed plan: resolve the snapshot table, one
/// extract per declared Scene, and drop the views of any Scene the host has no
/// content for. The returned value pairs the surviving views with their tasks;
/// the host fills their frame data in with `pairViewFrames()`.
[[nodiscard]] YA_GAME_RUNTIME_API ExtractedSceneRender extractHostSceneSnapshots(
    SceneRenderPlan   plan,
    TerrainProcessor* terrainProcessor);

} // namespace ya
