#include "Render3D/Common/SceneRenderScheduler.h"

#include "Scene/Core/Scene.h"

#include <unordered_map>
#include <cmath>
#include <utility>

namespace ya
{

namespace
{

/// Unique key of one Scene snapshot entry: the same Scene content is extracted
/// once per tick and shared by every View that references it.
struct SnapshotKey
{
    const Scene* scene     = nullptr;
    uint64_t sceneRevision = 0;

    bool operator==(const SnapshotKey& other) const
    {
        return scene == other.scene && sceneRevision == other.sceneRevision;
    }
};

struct SnapshotKeyHash
{
    size_t operator()(const SnapshotKey& key) const
    {
        const size_t sceneHash    = std::hash<const Scene*>{}(key.scene);
        const size_t revisionHash = std::hash<uint64_t>{}(key.sceneRevision);
        return sceneHash ^ (revisionHash + static_cast<size_t>(0x9e3779b9u) +
                            (sceneHash << 6u) + (sceneHash >> 2u));
    }
};

/// Group viewport tasks into family buckets. seal() calls this once, and
/// buildSceneSnapshots() calls it again when it had to drop unresolved views.
void buildViewFamilies(SceneRenderPlan& plan)
{
    plan.viewFamilies.clear();

    std::unordered_map<SceneViewFamilyKey, uint32_t, SceneViewFamilyKeyHash> familyIndices;
    familyIndices.reserve(plan.viewTasks.size());

    for (uint32_t taskIndex = 0; taskIndex < plan.viewTasks.size(); ++taskIndex) {
        SceneViewTask& task = plan.viewTasks[taskIndex];

        const SceneViewFamilyKey familyKey = makeSceneViewFamilyKey(task);
        auto [familyIt, familyInserted] =
            familyIndices.try_emplace(familyKey, static_cast<uint32_t>(plan.viewFamilies.size()));
        if (familyInserted) {
            plan.viewFamilies.push_back(SceneViewFamilyPlan{.key = familyKey});
        }

        const uint32_t familyIndex = familyIt->second;
        task.familyIndex           = familyIndex;
        plan.viewFamilies[familyIndex].viewTaskIndices.push_back(taskIndex);
    }
}

} // namespace

void SceneRenderScheduler::beginTick(uint64_t hostTick)
{
    _hostTick = hostTick;
    _declared.clear();
    _tickOpen = true;
}

bool SceneRenderScheduler::submit(SceneViewDesc desc)
{
    if (!_tickOpen || !desc.scene || desc.viewId == 0) {
        return false;
    }

    // A View's textures are sized from its rect, so a declaration that cannot
    // describe a single pixel (or is not a finite rect at all) is not a View.
    // Rejecting it here keeps the failure at the declaration edge instead of
    // letting it surface as an unsized resource in the middle of graph
    // building.
    const glm::vec2 declaredExtent = desc.outputRect.extent;
    if (!std::isfinite(declaredExtent.x) || !std::isfinite(declaredExtent.y)) {
        return false;
    }
    const Extent2D declaredPixels = Extent2D::fromVec2(declaredExtent);
    if (declaredPixels.width == 0 || declaredPixels.height == 0) {
        return false;
    }

    _declared.push_back(std::move(desc));
    return true;
}

SceneRenderPlan SceneRenderScheduler::seal()
{
    SceneRenderPlan plan{.hostTick = _hostTick};
    if (!_tickOpen) {
        return plan;
    }

    // Grouping only: one table entry per unique Scene, each still empty. The
    // caller fills them through buildSceneSnapshots(); nothing here reads
    // Scene/ECS content.
    std::unordered_map<SnapshotKey, uint32_t, SnapshotKeyHash> snapshotIndices;
    snapshotIndices.reserve(_declared.size());

    for (const SceneViewDesc& desc : _declared) {
        const SnapshotKey key{
            .scene         = desc.scene,
            .sceneRevision = desc.sceneRevision,
        };
        auto [it, inserted] = snapshotIndices.try_emplace(key, static_cast<uint32_t>(plan.snapshots.size()));
        if (inserted) {
            plan.snapshots.push_back(SceneSnapshotEntry{
                .scene         = desc.scene,
                .sceneRevision = desc.sceneRevision,
                .snapshot      = nullptr,
            });
        }

        // The declaration is carried over whole; only the plan's own output
        // identity and bookkeeping are added beside it.
        plan.viewTasks.push_back(SceneViewTask{
            .desc          = desc,
            .output =
                {
                    .viewId = desc.viewId,
                    .extent = Extent2D::fromVec2(desc.outputRect.extent),
                },
            .snapshotIndex = it->second,
        });
    }

    buildViewFamilies(plan);

    _tickOpen = false;
    return plan;
}

ExtractedSceneRender buildSceneSnapshots(SceneRenderPlan plan, const SceneSnapshotExtractor& extract)
{
    bool bUnresolved = false;
    for (SceneSnapshotEntry& entry : plan.snapshots) {
        if (entry.snapshot || !entry.scene) {
            continue;
        }
        if (extract) {
            entry.snapshot = extract(*entry.scene);
        }
        if (!entry.snapshot) {
            bUnresolved = true;
        }
    }

    if (bUnresolved) {
        // A View whose Scene content never arrived must not reach recording, and
        // the Views of the Scenes that did arrive still record this tick.
        std::erase_if(plan.viewTasks,
                      [&plan](const SceneViewTask& task) { return !plan.snapshotFor(task); });
        buildViewFamilies(plan);
    }

    ExtractedSceneRender extracted;
    extracted._plan = std::move(plan);
    return extracted;
}

void SceneRenderScheduler::clearTick()
{
    _declared.clear();
    _tickOpen = false;
}

} // namespace ya
