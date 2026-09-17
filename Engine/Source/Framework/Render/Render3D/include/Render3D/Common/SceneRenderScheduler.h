#pragma once

#include "Render3D/Common/RenderViewOutput.h"
#include "Render3D/Common/SceneViewDesc.h"
#include "Render3D/RenderFrameData.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <limits>
#include <vector>
#include <cstddef>

namespace ya
{

/// Graph / GPU-family grouping key. Same Scene snapshot + policy share one
/// `SceneViewFamilyPlan` and one `SceneFamilyResources`. Different Scenes do
/// not share skinning or scene GPU packets just because they share a flight.
/// The Scene is named by its handle: the plan holds one pointer per declared
/// View, so a parallel id would be a second spelling of the same fact.
struct SceneViewFamilyKey
{
    Scene*      scene         = nullptr;
    uint64_t    sceneRevision = 0;
    uint32_t    snapshotIndex = 0;
    uint64_t    policyId      = 0;

    bool operator==(const SceneViewFamilyKey&) const = default;
};

struct SceneViewFamilyKeyHash
{
    size_t operator()(const SceneViewFamilyKey& key) const
    {
        size_t hash = std::hash<const Scene*>{}(key.scene);
        const auto mix = [&hash](size_t value) {
            hash ^= value + static_cast<size_t>(0x9e3779b9u) + (hash << 6u) + (hash >> 2u);
        };
        mix(std::hash<uint64_t>{}(key.sceneRevision));
        mix(std::hash<uint32_t>{}(key.snapshotIndex));
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

    /// What the owner declared, verbatim. The Scene, the camera and the output
    /// rect are read from here; nothing re-lists them.
    SceneViewDesc desc{};
    /// Identity and extent of this View's offscreen output, derived from `desc`
    /// in `seal()`.
    RenderViewOutputDesc output{};

    uint32_t snapshotIndex = kInvalidSnapshotIndex;
    uint32_t familyIndex   = kInvalidFamilyIndex;
};

[[nodiscard]] inline SceneViewFamilyKey makeSceneViewFamilyKey(const SceneViewportTask& task)
{
    return SceneViewFamilyKey{
        .scene         = task.desc.scene,
        .sceneRevision = task.desc.sceneRevision,
        .snapshotIndex = task.snapshotIndex,
        .policyId      = task.desc.policyId,
    };
}

struct SceneSnapshotEntry
{
    /// The Scene this entry was declared for. Extraction reads it directly
    /// instead of searching the tick's declarations for a matching id, and the
    /// entry itself is keyed on this handle plus the revision.
    Scene*   scene         = nullptr;
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
        if (entry.scene != task.desc.scene || entry.sceneRevision != task.desc.sceneRevision) {
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
            if (task.desc.ownsHostViewport()) {
                return &task;
            }
        }
        return viewportTasks.empty() ? nullptr : &viewportTasks.front();
    }
};

[[nodiscard]] inline bool sceneViewOwnsHostViewport(const SceneViewportTask* task)
{
    return !task || task->desc.ownsHostViewport();
}

/// Every Scene an extracted plan actually produces content for, in snapshot-table
/// order. The table is deduplicated by (Scene, sceneRevision) already, so this
/// only collapses the remaining case: one Scene declared at two revisions.
[[nodiscard]] inline std::vector<Scene*> renderedScenes(const SceneRenderPlan& plan)
{
    std::vector<Scene*> scenes;
    scenes.reserve(plan.snapshots.size());
    for (const SceneSnapshotEntry& entry : plan.snapshots) {
        if (!entry.scene || !entry.snapshot) {
            continue;
        }
        if (std::find(scenes.begin(), scenes.end(), entry.scene) == scenes.end()) {
            scenes.push_back(entry.scene);
        }
    }
    return scenes;
}

/// One View inside an extracted plan: the task must point at
/// `plan.viewportTasks[i]`, and `frameData` is the host's View-owned
/// preparation for that task.
struct SceneViewRecording
{
    const SceneViewportTask* task      = nullptr;
    RenderFrameData*         frameData = nullptr;
};

/// Extracts the immutable Scene snapshot for one declared Scene. Scene content
/// belongs to the host (ECS traversal, terrain, ...), so the scheduler asks for
/// it instead of reaching into the Scene itself. A null result means the host
/// has no content for that Scene this tick.
using SceneSnapshotExtractor =
    std::function<std::shared_ptr<const SceneSnapshot>(Scene& scene)>;

/// A sealed plan whose snapshot table has been resolved, paired with the host's
/// per-view frame data.
///
/// Only `buildSceneSnapshots()` can build the plan half, and the default state
/// is the empty UI-only plan, so a plan that still holds empty snapshot entries
/// cannot reach `RenderFrameCoordinator::record`: "forgot to extract" is a
/// compile error instead of a runtime log. Recordings enter through
/// `pairViewFrames()` alone, so the two lists cannot drift apart the way
/// hand-filled parallel arrays can.
class ExtractedSceneRender
{
  public:
    ExtractedSceneRender() = default;
    ExtractedSceneRender(const ExtractedSceneRender&)            = delete;
    ExtractedSceneRender& operator=(const ExtractedSceneRender&) = delete;
    /// Moving a vector transfers its storage, so the task pointers held by the
    /// recordings keep pointing at this plan's own tasks.
    ExtractedSceneRender(ExtractedSceneRender&&)            = default;
    ExtractedSceneRender& operator=(ExtractedSceneRender&&) = default;

    /// Pair one recording with every surviving viewport task, in plan order.
    /// `frameData` ends up with exactly one slot per surviving view; slots left
    /// over from a wider tick are released so they cannot keep that tick's Scene
    /// snapshot alive. A UI-only tick leaves the single slot the camera packet
    /// reads and drops any stale Scene snapshot.
    void pairViewFrames(std::vector<RenderFrameData>& frameData)
    {
        _views.clear();
        if (_plan.viewportTasks.empty()) {
            frameData.resize(1);
            frameData.front().clear();
            return;
        }

        frameData.resize(_plan.viewportTasks.size());
        _views.reserve(_plan.viewportTasks.size());
        for (size_t index = 0; index < _plan.viewportTasks.size(); ++index) {
            _views.push_back(SceneViewRecording{
                .task      = &_plan.viewportTasks[index],
                .frameData = &frameData[index],
            });
        }
    }

    [[nodiscard]] bool empty() const { return _plan.viewportTasks.empty(); }

    [[nodiscard]] const SceneRenderPlan& plan() const { return _plan; }

    [[nodiscard]] const std::vector<SceneViewRecording>& views() const { return _views; }

    [[nodiscard]] const SceneViewportTask* primaryTask() const { return _plan.displayRootTask(); }

    [[nodiscard]] std::shared_ptr<const SceneSnapshot> snapshotFor(const SceneViewportTask& task) const
    {
        return _plan.snapshotFor(task);
    }

  private:
    friend ExtractedSceneRender buildSceneSnapshots(SceneRenderPlan plan, const SceneSnapshotExtractor& extract);

    SceneRenderPlan                 _plan;
    std::vector<SceneViewRecording> _views;
};

/// Explicit extraction step: consumed by the host after `seal()` and before
/// recording. Takes the sealed plan by value, fills every snapshot-table entry
/// from the Scene its declaration named, then drops the views of entries it
/// could not resolve and regroups the families: a Scene without content
/// contributes no view, and one unresolved Scene must not take the rest of the
/// tick down with it.
[[nodiscard]] YA_RENDER_3D_API ExtractedSceneRender buildSceneSnapshots(SceneRenderPlan plan,
                                                                       const SceneSnapshotExtractor& extract);

/// Tick-local declaration collector. It does not own Scene/ECS objects and does
/// not record GPU commands; RenderFrameCoordinator consumes the sealed immutable plan.
/// Its two phases are separate: `seal()` groups declarations, and extraction
/// is the caller's explicit `buildSceneSnapshots()` step.
class SceneRenderScheduler
{
  public:
    void beginTick(uint64_t hostTick);
    /// Declare one View for the open tick. The desc is stored as declared, so
    /// sealing copies no field of it.
    bool submit(SceneViewDesc desc);
    [[nodiscard]] SceneRenderPlan seal();
    void clearTick();

    [[nodiscard]] bool isTickOpen() const { return _tickOpen; }
    [[nodiscard]] uint64_t hostTick() const { return _hostTick; }
    [[nodiscard]] size_t declaredViewCount() const { return _declared.size(); }

  private:
    uint64_t                   _hostTick = 0;
    bool                       _tickOpen = false;
    std::vector<SceneViewDesc> _declared;
};

} // namespace ya
