#include "SceneRenderScheduler.h"

#include <unordered_map>
#include <utility>

namespace ya
{

void SceneRenderScheduler::beginTick(uint64_t hostTick)
{
    _hostTick = hostTick;
    _requests.clear();
    _tickOpen = true;
}

bool SceneRenderScheduler::submit(SceneRenderRequest request)
{
    if (!_tickOpen || request.sceneId == 0 || request.viewId == 0 || !request.buildSnapshot) {
        return false;
    }

    _requests.push_back(std::move(request));
    return true;
}

SceneRenderPlan SceneRenderScheduler::seal()
{
    SceneRenderPlan plan{.hostTick = _hostTick};
    if (!_tickOpen) {
        return plan;
    }

    struct SnapshotKey
    {
        SceneId sceneId = 0;
        uint64_t sceneRevision = 0;

        bool operator==(const SnapshotKey& other) const
        {
            return sceneId == other.sceneId && sceneRevision == other.sceneRevision;
        }
    };
    struct SnapshotKeyHash
    {
        size_t operator()(const SnapshotKey& key) const
        {
            const size_t sceneHash = std::hash<SceneId>{}(key.sceneId);
            const size_t revisionHash = std::hash<uint64_t>{}(key.sceneRevision);
            return sceneHash ^ (revisionHash + static_cast<size_t>(0x9e3779b9u) +
                                (sceneHash << 6u) + (sceneHash >> 2u));
        }
    };

    std::unordered_map<SnapshotKey, uint32_t, SnapshotKeyHash> snapshotIndices;
    snapshotIndices.reserve(_requests.size());

    for (const auto& request : _requests) {
        const SnapshotKey key{
            .sceneId = request.sceneId,
            .sceneRevision = request.sceneRevision,
        };
        auto [it, inserted] = snapshotIndices.try_emplace(key, static_cast<uint32_t>(plan.snapshots.size()));
        if (inserted) {
            plan.snapshots.push_back(SceneSnapshotEntry{
                .sceneId = request.sceneId,
                .sceneRevision = request.sceneRevision,
                .snapshot = request.buildSnapshot(),
            });
        }
        const uint32_t snapshotIndex = it->second;
        const auto& snapshot = plan.snapshots[snapshotIndex].snapshot;
        if (!snapshot) {
            continue;
        }

        plan.viewportTasks.push_back(SceneViewportTask{
            .sceneId            = request.sceneId,
            .sceneRevision      = request.sceneRevision,
            .viewId             = request.viewId,
            .familyId           = request.familyId,
            .view               = request.view,
            .projection         = request.projection,
            .viewProjection     = request.viewProjection,
            .cameraPos          = request.cameraPos,
            .viewportRect       = request.viewportRect,
            .renderFlags        = request.renderFlags,
            .composeOntoViewId  = request.composeOntoViewId,
            .composeRect        = request.composeRect,
            .output =
                {
                    .viewId = request.viewId,
                    .extent = Extent2D::fromVec2(request.viewportRect.extent),
                },
            .snapshotIndex = snapshotIndex,
        });
    }

    std::unordered_map<SceneViewFamilyKey, uint32_t, SceneViewFamilyKeyHash> familyIndices;
    familyIndices.reserve(plan.viewportTasks.size());
    for (uint32_t taskIndex = 0; taskIndex < plan.viewportTasks.size(); ++taskIndex) {
        SceneViewportTask& task = plan.viewportTasks[taskIndex];
        const SceneViewFamilyKey familyKey = makeSceneViewFamilyKey(task);
        auto [familyIt, familyInserted] =
            familyIndices.try_emplace(familyKey, static_cast<uint32_t>(plan.viewFamilies.size()));
        if (familyInserted) {
            plan.viewFamilies.push_back(SceneViewFamilyPlan{.key = familyKey});
        }
        const uint32_t familyIndex = familyIt->second;
        task.familyIndex = familyIndex;
        plan.viewFamilies[familyIndex].viewportTaskIndices.push_back(taskIndex);
    }

    _tickOpen = false;
    return plan;
}

void SceneRenderScheduler::clearTick()
{
    _requests.clear();
    _tickOpen = false;
}

} // namespace ya
