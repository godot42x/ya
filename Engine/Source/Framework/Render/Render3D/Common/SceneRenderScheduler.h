#pragma once

#include "Render3D/Common/RenderViewOutput.h"
#include "Render3D/RenderFrameData.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <limits>
#include <vector>

namespace ya
{

using SceneId = uint64_t;
using SceneViewId = uint64_t;

struct SceneRenderRequest
{
    SceneId     sceneId  = 0;
    uint64_t    sceneRevision = 0;
    SceneViewId viewId   = 0;
    uint64_t    familyId = 0;

    glm::mat4 view           = glm::mat4(1.0f);
    glm::mat4 projection     = glm::mat4(1.0f);
    glm::mat4 viewProjection = glm::mat4(1.0f);
    glm::vec3 cameraPos      = glm::vec3(0.0f);
    Rect2D    viewportRect{};
    uint32_t  renderFlags = 0;

    /// Product code owns the Scene. The scheduler invokes this once per Scene
    /// in the frame and retains only the immutable result in the plan.
    std::function<std::shared_ptr<const SceneFrameSnapshot>()> buildSnapshot;
};

struct SceneViewportTask
{
    static constexpr uint32_t kInvalidSnapshotIndex = std::numeric_limits<uint32_t>::max();

    SceneId     sceneId  = 0;
    uint64_t    sceneRevision = 0;
    SceneViewId viewId   = 0;
    uint64_t    familyId = 0;

    glm::mat4 view           = glm::mat4(1.0f);
    glm::mat4 projection     = glm::mat4(1.0f);
    glm::mat4 viewProjection = glm::mat4(1.0f);
    glm::vec3 cameraPos      = glm::vec3(0.0f);
    Rect2D    viewportRect{};
    uint32_t  renderFlags = 0;
    RenderViewOutputDesc output{};

    uint32_t snapshotIndex = kInvalidSnapshotIndex;
};

struct SceneSnapshotEntry
{
    SceneId sceneId = 0;
    uint64_t sceneRevision = 0;
    std::shared_ptr<const SceneFrameSnapshot> snapshot;
};

struct SceneRenderPlan
{
    uint64_t frameId = 0;
    std::vector<SceneSnapshotEntry> snapshots;
    std::vector<SceneViewportTask> viewportTasks;

    [[nodiscard]] bool empty() const { return viewportTasks.empty(); }

    [[nodiscard]] std::shared_ptr<const SceneFrameSnapshot> snapshotFor(
        const SceneViewportTask& task) const
    {
        if (task.snapshotIndex >= snapshots.size()) {
            return nullptr;
        }
        const SceneSnapshotEntry& entry = snapshots[task.snapshotIndex];
        if (entry.sceneId != task.sceneId || entry.sceneRevision != task.sceneRevision) {
            return nullptr;
        }
        return entry.snapshot;
    }
};

/// Frame-local request collector. It does not own Scene/ECS objects and does
/// not record GPU commands; RenderRuntime consumes the sealed immutable plan.
class SceneRenderScheduler
{
  public:
    void beginFrame(uint64_t frameId);
    bool submit(SceneRenderRequest request);
    [[nodiscard]] SceneRenderPlan seal();
    void clearFrame();

    [[nodiscard]] bool isFrameOpen() const { return _frameOpen; }
    [[nodiscard]] uint64_t frameId() const { return _frameId; }
    [[nodiscard]] size_t pendingRequestCount() const { return _requests.size(); }

  private:
    uint64_t                    _frameId = 0;
    bool                        _frameOpen = false;
    std::vector<SceneRenderRequest> _requests;
};

} // namespace ya
