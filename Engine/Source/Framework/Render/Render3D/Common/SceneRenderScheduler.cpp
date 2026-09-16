#include "SceneRenderScheduler.h"

#include <unordered_map>
#include <utility>

namespace ya
{

void SceneRenderScheduler::beginFrame(uint64_t frameId)
{
    _frameId = frameId;
    _requests.clear();
    _frameOpen = true;
}

bool SceneRenderScheduler::submit(SceneRenderRequest request)
{
    if (!_frameOpen || request.sceneId == 0 || request.viewId == 0 || !request.buildSnapshot) {
        return false;
    }

    _requests.push_back(std::move(request));
    return true;
}

SceneRenderPlan SceneRenderScheduler::seal()
{
    SceneRenderPlan plan{.frameId = _frameId};
    if (!_frameOpen) {
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
            .sceneId       = request.sceneId,
            .sceneRevision = request.sceneRevision,
            .viewId        = request.viewId,
            .familyId      = request.familyId,
            .view          = request.view,
            .projection    = request.projection,
            .viewProjection = request.viewProjection,
            .cameraPos     = request.cameraPos,
            .viewportRect  = request.viewportRect,
            .renderFlags   = request.renderFlags,
            .output =
                {
                    .viewId = request.viewId,
                    .extent = Extent2D::fromVec2(request.viewportRect.extent),
                },
            .snapshotIndex = snapshotIndex,
        });
    }

    _frameOpen = false;
    return plan;
}

void SceneRenderScheduler::clearFrame()
{
    _requests.clear();
    _frameOpen = false;
}

} // namespace ya
