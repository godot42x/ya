#pragma once

#include "Core/Base.h"
#include "Render3D/Common/RenderFrameInputs.h"
#include "Render3D/Common/SceneRenderScheduler.h"

#include <span>
#include <vector>

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

/// Submit every live Scene viewport. Same Scene* shares one extract builder;
/// different Scenes produce different snapshot table entries and families.
/// Null `scene` entries are skipped. The scheduler frame must already be open.
[[nodiscard]] YA_GAME_RUNTIME_API bool submitHostSceneViews(SceneRenderScheduler&                scheduler,
                                                            TerrainProcessor*                    terrainProcessor,
                                                            std::span<const HostSceneViewSubmit> views);

[[nodiscard]] YA_GAME_RUNTIME_API Scene* derivedSceneForHostView(std::span<const HostSceneViewSubmit> views,
                                                                 const SceneViewportTask&             task);

} // namespace ya
