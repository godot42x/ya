#include "OffscreenJobRunner.h"

#include "GameRuntime/App.h"

#include "RHI/Core/OffscreenJob.h"

namespace ya
{

void queueOffscreenJob(App* app, IRender* render, const std::shared_ptr<OffscreenJobState>& job)
{
    OffscreenJobQueueService queueService{};
    if (app) {
        queueService.enqueue = [app](const std::shared_ptr<OffscreenJobState>& queuedJob, std::function<void(ICommandBuffer*)> task)
        {
            app->getTaskManager().enqueueOffscreenTask(queuedJob, std::move(task));
        };
    }

    queueOffscreenJob(queueService, render, job);
}

} // namespace ya
