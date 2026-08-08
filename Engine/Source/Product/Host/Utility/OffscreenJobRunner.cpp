#include "OffscreenJobRunner.h"

#include "Product/Host/App.h"

#include "Foundation/RHI/Core/OffscreenJob.h"
#include "Foundation/Core/Common/DeferredDeletionQueue.h"

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
            app->getTaskManager().enqueueOffscreenTask(queuedJob, std::move(task));
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

    auto outputImage = job->createOutputFn ? job->createOutputFn(render) : nullptr;
    if (job->createOutputFn && !outputImage) {
        failOffscreenJob(job);
        return;
    }

    job->phase = EOffscreenJobPhase::Queued;
    queueService.enqueue(
        job,
        [job, outputImage = std::move(outputImage)](ICommandBuffer* cmdBuf) mutable
        {
            if (!job || job->bCancelled || !cmdBuf || !job->executeFn) {
                if (outputImage) {
                    DeferredDeletionQueue::get().retireResource(outputImage);
                    outputImage = nullptr;
                }
                failOffscreenJob(job);
                return;
            }

            const size_t retainedBegin = cmdBuf->retainedResources.size();
            const bool bSuccess = job->executeFn(cmdBuf, outputImage.get());
            if (!bSuccess || job->bCancelled) {
                if (outputImage) {
                    DeferredDeletionQueue::get().retireResource(outputImage);
                    outputImage = nullptr;
                }
                failOffscreenJob(job);
                return;
            }

            if (job->result) {
                job->result->outputImage = std::move(outputImage);
                if (retainedBegin < cmdBuf->retainedResources.size()) {
                    job->result->retainedResources.insert(
                        job->result->retainedResources.end(),
                        cmdBuf->retainedResources.begin() + static_cast<std::ptrdiff_t>(retainedBegin),
                        cmdBuf->retainedResources.end());
                }
                if (job->result->outputImage && !job->result->retainedResources.empty()) {
                    auto& imageRetained = job->result->outputImage->retainedResources;
                    imageRetained.insert(
                        imageRetained.end(),
                        job->result->retainedResources.begin(),
                        job->result->retainedResources.end());
                }
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
    if (job->result && job->result->outputImage) {
        DeferredDeletionQueue::get().retireResource(job->result->outputImage);
        job->result->outputImage = nullptr;
    }
    job.reset();
}

} // namespace ya
