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

    job->bTaskFinished  = true;
    job->bTaskSucceeded = false;
}
} // namespace

void queueOffscreenJob(App* app, IRender* render, const std::shared_ptr<OffscreenJobState>& job)
{
    if (!job || !app || !render || !job->isReadyToQueue()) {
        failOffscreenJob(job);
        return;
    }

    auto outputTexture = job->createOutputFn ? job->createOutputFn(render) : nullptr;
    if (!outputTexture) {
        failOffscreenJob(job);
        return;
    }

    job->bTaskQueued = true;
    app->taskManager.enqueueOffscreenTask(
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
            job->bTaskFinished  = true;
            job->bTaskSucceeded = true;
        });
}

void cancelOffscreenJob(std::shared_ptr<OffscreenJobState>& job)
{
    if (!job) {
        return;
    }

    job->bCancelled = true;
    if (job->result && job->result->outputTexture) {
        DeferredDeletionQueue::get().retireResource(job->result->outputTexture);
        job->result->outputTexture = nullptr;
    }
    job.reset();
}

} // namespace ya