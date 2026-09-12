#include "SceneRenderScheduler.h"

#include <unordered_map>

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

    std::unordered_map<SceneId, std::shared_ptr<const WorldFrameSnapshot>> snapshots;
    snapshots.reserve(_requests.size());

    for (const auto& request : _requests) {
        auto [it, inserted] = snapshots.try_emplace(request.sceneId);
        if (inserted) {
            it->second = request.buildSnapshot();
        }
        if (!it->second) {
            continue;
        }

        plan.viewportTasks.push_back(SceneViewportTask{
            .sceneId       = request.sceneId,
            .viewId        = request.viewId,
            .familyId      = request.familyId,
            .view          = request.view,
            .projection    = request.projection,
            .viewProjection = request.viewProjection,
            .cameraPos     = request.cameraPos,
            .viewportRect  = request.viewportRect,
            .renderFlags   = request.renderFlags,
            .snapshot      = it->second,
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
