#include "OffscreenJobRunner.h"

#include "Runtime/App/App.h"

#include "Render/Core/OffscreenJob.h"
#include "Resource/DeferredDeletionQueue.h"

namespace ya
{

namespace
{
void failOffscreenJob(const std::shared_ptr<OffscreenJobState>& job)
{
    if (!job) {
        return;
    }

    if (job->bCancelled || job->phase == EOffscreenJobPhase::Cancelled) {
        job->phase = EOffscreenJobPhase::Cancelled;
        return;
    }

    job->phase = EOffscreenJobPhase::Failed;
}
} // namespace

void queueOffscreenJob(App* app, IRender* render, const std::shared_ptr<OffscreenJobState>& job)
{
    OffscreenJobQueueService queueService{};
    if (app) {
        queueService.enqueue = [app](const std::shared_ptr<OffscreenJobState>& queuedJob, std::function<void(ICommandBuffer*)> task)
        {
            app->taskManager.enqueueOffscreenTask(queuedJob, std::move(task));
        };
    }

    queueOffscreenJob(queueService, render, job);
}

void queueOffscreenJob(const OffscreenJobQueueService& queueService, IRender* render, const std::shared_ptr<OffscreenJobState>& job)
{
    if (!job || !queueService.enqueue || !render || !job->isReadyToQueue()) {
        failOffscreenJob(job);
        return;
    }

    auto outputTexture = job->createOutputFn ? job->createOutputFn(render) : nullptr;
    if (job->createOutputFn && !outputTexture) {
        failOffscreenJob(job);
        return;
    }

    job->phase = EOffscreenJobPhase::Queued;
    queueService.enqueue(
        job,
        [job, outputTexture = std::move(outputTexture)](ICommandBuffer* cmdBuf) mutable
        {
            if (!job || job->bCancelled || !cmdBuf || !job->executeFn) {
                if (outputTexture) {
                    DeferredDeletionQueue::get().retireResource(outputTexture);
                    outputTexture = nullptr;
                }
                failOffscreenJob(job);
                return;
            }

            const bool bSuccess = job->executeFn(cmdBuf, outputTexture.get());
            if (!bSuccess || job->bCancelled) {
                if (outputTexture) {
                    DeferredDeletionQueue::get().retireResource(outputTexture);
                    outputTexture = nullptr;
                }
                failOffscreenJob(job);
                return;
            }

            if (job->result) {
                job->result->outputTexture = std::move(outputTexture);
            }
            job->phase = EOffscreenJobPhase::Recorded;
        });
}

void cancelOffscreenJob(std::shared_ptr<OffscreenJobState>& job)
{
    if (!job) {
        return;
    }

    job->bCancelled = true;
    job->phase      = EOffscreenJobPhase::Cancelled;
    if (job->result && job->result->outputTexture) {
        DeferredDeletionQueue::get().retireResource(job->result->outputTexture);
        job->result->outputTexture = nullptr;
    }
    job.reset();
}

} // namespace ya
