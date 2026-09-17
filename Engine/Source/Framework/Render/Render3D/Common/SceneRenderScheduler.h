#pragma once

#include "Render3D/Common/RenderViewOutput.h"
#include "Render3D/RenderFrameData.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <limits>
#include <vector>
#include <cstddef>

namespace ya
{

using SceneId = uint64_t;
using SceneViewId = uint64_t;

/// Stable persistent-key slot for the host WorldView. Overlay Views use other
/// host-assigned ids; they must not resize this View's output identity.
inline constexpr SceneViewId kPrimarySceneViewId = 1;

/// One View the host wants rendered this tick. Pure declaration: it names the
/// Scene content, the camera and the output, and carries no Scene handle and no
/// extraction callback. Grouping happens in `seal()`; content is resolved by
/// the separate `buildSceneSnapshots()` step.
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
    /// This View's own offscreen camera rect (origin at its RT top-left).
    /// Not chrome widget offset, and not the compose dest on another View.
    Rect2D    viewportRect{};
    uint32_t  renderFlags = 0;
    /// 0: this View is a display root (host viewport identity). Non-zero: blit
    /// `composeRect` onto that View's display RT after recording.
    SceneViewId composeOntoViewId = 0;
    Rect2D      composeRect{};
};

/// Graph / GPU-family grouping key. Same Scene snapshot + policy share one
/// `SceneViewFamilyPlan` and one `SceneFamilyResources`. Different Scenes do
/// not share skinning or scene GPU packets just because they share a flight.
struct SceneViewFamilyKey
{
    SceneId     sceneId       = 0;
    uint64_t    sceneRevision = 0;
    uint32_t    snapshotIndex = 0;
    uint32_t    renderFlags   = 0;
    uint64_t    policyId      = 0;

    bool operator==(const SceneViewFamilyKey&) const = default;
};

struct SceneViewFamilyKeyHash
{
    size_t operator()(const SceneViewFamilyKey& key) const
    {
        size_t hash = std::hash<SceneId>{}(key.sceneId);
        const auto mix = [&hash](size_t value) {
            hash ^= value + static_cast<size_t>(0x9e3779b9u) + (hash << 6u) + (hash >> 2u);
        };
        mix(std::hash<uint64_t>{}(key.sceneRevision));
        mix(std::hash<uint32_t>{}(key.snapshotIndex));
        mix(std::hash<uint32_t>{}(key.renderFlags));
        mix(std::hash<uint64_t>{}(key.policyId));
        return hash;
    }
};

struct SceneViewFamilyPlan
{
    SceneViewFamilyKey     key;
    std::vector<uint32_t>  viewportTaskIndices;
};

/// Typed outputs of one family graph. Coordinator publishes these; it does not
/// ask the renderer for a current View image.
struct ViewFamilyRenderResult
{
    SceneViewFamilyKey            key{};
    std::vector<RenderViewOutput> views;
};

struct SceneViewportTask
{
    static constexpr uint32_t kInvalidSnapshotIndex = std::numeric_limits<uint32_t>::max();
    static constexpr uint32_t kInvalidFamilyIndex   = std::numeric_limits<uint32_t>::max();

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
    SceneViewId composeOntoViewId = 0;
    Rect2D      composeRect{};
    RenderViewOutputDesc output{};

    uint32_t snapshotIndex = kInvalidSnapshotIndex;
    uint32_t familyIndex   = kInvalidFamilyIndex;

    [[nodiscard]] bool ownsHostViewport() const { return composeOntoViewId == 0; }
};

[[nodiscard]] inline SceneViewFamilyKey makeSceneViewFamilyKey(const SceneViewportTask& task)
{
    return SceneViewFamilyKey{
        .sceneId        = task.sceneId,
        .sceneRevision  = task.sceneRevision,
        .snapshotIndex  = task.snapshotIndex,
        .renderFlags    = task.renderFlags,
        .policyId       = task.familyId,
    };
}

struct SceneSnapshotEntry
{
    SceneId sceneId = 0;
    uint64_t sceneRevision = 0;
    /// Empty until `buildSceneSnapshots()` resolves it. A null snapshot means
    /// the host had no content for that Scene this tick.
    std::shared_ptr<const SceneSnapshot> snapshot;
};

struct SceneRenderPlan
{
    uint64_t hostTick = 0;
    std::vector<SceneSnapshotEntry> snapshots;
    std::vector<SceneViewportTask> viewportTasks;
    std::vector<SceneViewFamilyPlan> viewFamilies;

    [[nodiscard]] bool empty() const { return viewportTasks.empty(); }

    [[nodiscard]] std::shared_ptr<const SceneSnapshot> snapshotFor(
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

    [[nodiscard]] const SceneViewFamilyPlan* familyFor(const SceneViewportTask& task) const
    {
        if (task.familyIndex >= viewFamilies.size()) {
            return nullptr;
        }
        const SceneViewFamilyPlan& family = viewFamilies[task.familyIndex];
        if (family.key != makeSceneViewFamilyKey(task)) {
            return nullptr;
        }
        return &family;
    }

    [[nodiscard]] const SceneViewportTask* displayRootTask() const
    {
        for (const auto& task : viewportTasks) {
            if (task.ownsHostViewport()) {
                return &task;
            }
        }
        return viewportTasks.empty() ? nullptr : &viewportTasks.front();
    }
};

[[nodiscard]] inline bool sceneViewOwnsHostViewport(const SceneViewportTask* task)
{
    return !task || task->ownsHostViewport();
}

/// Resolves the immutable Scene snapshot for one (sceneId, sceneRevision).
/// Scene content belongs to the host, so the scheduler asks for it instead of
/// reaching into ECS itself.
using SceneSnapshotResolver =
    std::function<std::shared_ptr<const SceneSnapshot>(SceneId sceneId, uint64_t sceneRevision)>;

/// Explicit extraction step, run by the host after `seal()` and before
/// recording. Fills every empty entry in the plan's snapshot table, then drops
/// the views of entries it could not resolve and regroups the families: a Scene
/// without content contributes no view, and one unresolved Scene must not take
/// the rest of the tick down with it. Returns the number of unresolved entries.
[[nodiscard]] YA_RENDER_3D_API uint32_t buildSceneSnapshots(SceneRenderPlan& plan,
                                                           const SceneSnapshotResolver& resolve);

/// Tick-local request collector. It does not own Scene/ECS objects and does
/// not record GPU commands; RenderFrameCoordinator consumes the sealed immutable plan.
/// Its two phases are separate: `seal()` groups declarations, and extraction
/// is the caller's explicit `buildSceneSnapshots()` step.
class SceneRenderScheduler
{
  public:
    void beginTick(uint64_t hostTick);
    bool submit(SceneRenderRequest request);
    [[nodiscard]] SceneRenderPlan seal();
    void clearTick();

    [[nodiscard]] bool isTickOpen() const { return _tickOpen; }
    [[nodiscard]] uint64_t hostTick() const { return _hostTick; }
    [[nodiscard]] size_t pendingRequestCount() const { return _requests.size(); }

  private:
    uint64_t                    _hostTick = 0;
    bool                        _tickOpen = false;
    std::vector<SceneRenderRequest> _requests;
};

} // namespace ya
