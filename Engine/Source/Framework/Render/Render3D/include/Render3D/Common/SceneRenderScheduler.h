#pragma once

#include "Graph/RenderGraph.h"
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
    std::vector<uint32_t>  viewTaskIndices;
};

/// Typed outputs of one family graph. Coordinator publishes these; it does not
/// ask the renderer for a current View image.
struct ViewFamilyRenderResult
{
    SceneViewFamilyKey            key{};
    std::vector<RenderViewOutput> views;
    /// The topology this family's graph compiled to. Empty unless the graph
    /// executed successfully, so diagnostics never show a graph that did not
    /// run; the per-frame collection lives on the device publication side.
    RGTopologyDescription         topology{};
};

struct SceneViewTask
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

[[nodiscard]] inline SceneViewFamilyKey makeSceneViewFamilyKey(const SceneViewTask& task)
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
    std::vector<SceneViewTask> viewTasks;
    std::vector<SceneViewFamilyPlan> viewFamilies;

    [[nodiscard]] bool empty() const { return viewTasks.empty(); }

    [[nodiscard]] std::shared_ptr<const SceneSnapshot> snapshotFor(
        const SceneViewTask& task) const
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

    [[nodiscard]] const SceneViewFamilyPlan* familyFor(const SceneViewTask& task) const
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

    /// The View whose output the host viewport displays, or null when this tick
    /// declares none.
    ///
    /// A View owns the host viewport by declaration (`bDisplayRoot`), never by
    /// holding a well-known id, so this answers the same question every
    /// caller asks: the pipelines' display-root branch and the publish identity
    /// all read this one predicate. Null is a real answer -- the tick still
    /// renders what its other Views asked for, nothing is displayed on the host
    /// viewport, and no caller gets a substitute View it did not ask for. With
    /// several display roots (one per surface, the multi-window case) the first
    /// wins until a surface-scoped identity exists.
    [[nodiscard]] const SceneViewTask* displayRootTask() const
    {
        for (const auto& task : viewTasks) {
            if (task.desc.bDisplayRoot) {
                return &task;
            }
        }
        return nullptr;
    }
};

/// A View owns the host viewport by declaration. A recording without a task
/// owns nothing: there is no View, so there is no host viewport belonging to it.
[[nodiscard]] inline bool sceneViewIsDisplayRoot(const SceneViewTask* task)
{
    return task && task->desc.bDisplayRoot;
}

/// Whether this tick declares the named View.
///
/// This is the whole answer to "does that View still exist this tick": the
/// tick's declarations are what makes a View real, so a caller deciding what a
/// View's absence means reads the tick rather than guessing from how long ago
/// the View was recorded. The identity compared here is the declaration's, and
/// it is the same id the View's output is published under -- seal() derives the
/// one from the other.
[[nodiscard]] inline bool planDeclaresView(const SceneRenderPlan& plan, SceneViewId viewId)
{
    return std::any_of(plan.viewTasks.begin(), plan.viewTasks.end(),
                       [viewId](const SceneViewTask& task) { return task.desc.viewId == viewId; });
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
/// `plan.viewTasks[i]`, and `frameData` is the host's View-owned
/// preparation for that task.
struct SceneViewRecording
{
    const SceneViewTask* task      = nullptr;
    RenderFrameData*         frameData = nullptr;
};

/// Extracts the immutable Scene snapshot for one declared Scene. Scene content
/// belongs to the host (ECS traversal, terrain, ...), so the scheduler asks for
/// it instead of reaching into the Scene itself. A null result means the host
/// has no content for that Scene this tick.
using SceneSnapshotExtractor =
    std::function<std::shared_ptr<const SceneSnapshot>(Scene& scene)>;

/// A sealed plan whose snapshot table has been resolved, paired with this tick's
/// per-view prepared data. The packets are consumed synchronously while the
/// renderer builds and executes the graph; they are not GPU-lifetime state and
/// must not live on AppRenderState merely because the old implementation reused
/// a per-flight vector.
///
/// Only `buildSceneSnapshots()` can build the plan half, and the default state
/// is the empty UI-only plan, so a plan that still holds empty snapshot entries
/// cannot reach a recording: "forgot to extract" is a
/// compile error instead of a runtime log. Recordings enter through
/// `pairViewFrames()` alone, so the two lists cannot drift apart the way
/// hand-filled parallel arrays can.
class ExtractedSceneRender
{
  public:
    ExtractedSceneRender() = default;
    ExtractedSceneRender(const ExtractedSceneRender&)            = delete;
    ExtractedSceneRender& operator=(const ExtractedSceneRender&) = delete;
    /// Recordings borrow the plan and packet vectors, so a move must rebind
    /// those pointers to the destination object's storage.
    ExtractedSceneRender(ExtractedSceneRender&& other) noexcept
        : _plan(std::move(other._plan)),
          _views(std::move(other._views)),
          _frameData(std::move(other._frameData))
    {
        rebindRecordings();
    }

    ExtractedSceneRender& operator=(ExtractedSceneRender&& other) noexcept
    {
        if (this != &other) {
            _plan      = std::move(other._plan);
            _views     = std::move(other._views);
            _frameData = std::move(other._frameData);
            rebindRecordings();
        }
        return *this;
    }

    /// Pair one recording with every surviving viewport task, in plan order.
    /// The packet storage is owned by this tick's extracted render value, so
    /// recordings cannot point into long-lived App state or a different flight.
    /// A UI-only tick owns no View packet and therefore produces no recording.
    void pairViewFrames()
    {
        _views.clear();
        _frameData.clear();
        if (_plan.viewTasks.empty()) {
            return;
        }

        _frameData.resize(_plan.viewTasks.size());
        _views.reserve(_plan.viewTasks.size());
        for (size_t index = 0; index < _plan.viewTasks.size(); ++index) {
            _views.push_back(SceneViewRecording{
                .task      = &_plan.viewTasks[index],
                .frameData = &_frameData[index],
            });
        }
    }

    [[nodiscard]] bool empty() const { return _plan.viewTasks.empty(); }

    [[nodiscard]] const SceneRenderPlan& plan() const { return _plan; }

    [[nodiscard]] const std::vector<SceneViewRecording>& views() const { return _views; }

    [[nodiscard]] const SceneViewTask* displayRootTask() const { return _plan.displayRootTask(); }

    [[nodiscard]] std::shared_ptr<const SceneSnapshot> snapshotFor(const SceneViewTask& task) const
    {
        return _plan.snapshotFor(task);
    }

  private:
    friend ExtractedSceneRender buildSceneSnapshots(SceneRenderPlan plan, const SceneSnapshotExtractor& extract);

    void rebindRecordings()
    {
        for (size_t index = 0; index < _views.size(); ++index) {
            SceneViewRecording& recording = _views[index];
            recording.task = index < _plan.viewTasks.size() ? &_plan.viewTasks[index] : nullptr;
            recording.frameData = index < _frameData.size() ? &_frameData[index] : nullptr;
        }
    }

    SceneRenderPlan                 _plan;
    std::vector<SceneViewRecording> _views;
    std::vector<RenderFrameData>    _frameData;
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
/// not record GPU commands; the application's recording order consumes the sealed immutable plan.
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
