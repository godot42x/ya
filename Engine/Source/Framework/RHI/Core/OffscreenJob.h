#pragma once

#include "RHI/Core/ImageResource.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ya
{

struct IRender;
struct ICommandBuffer;
struct OffscreenJobState;

/// Low-level offscreen render-task contract. The Host implements enqueue
/// (through its task manager); render features only depend on this contract,
/// never on Host types.
struct OffscreenJobQueueService
{
    std::function<void(const std::shared_ptr<OffscreenJobState>&, std::function<void(ICommandBuffer*)>)> enqueue;
};

/// Queue a job through the injected queue service (output-image creation +
/// record callback with keep-alive bookkeeping). Implementation lives in the
/// RHI layer; the Host only provides the enqueue sink.
YA_RHI_API void queueOffscreenJob(const OffscreenJobQueueService& queueService, IRender* render, const std::shared_ptr<OffscreenJobState>& job);

/// Cancel a queued/pending offscreen job and retire its GPU resources.
YA_RHI_API void cancelOffscreenJob(std::shared_ptr<OffscreenJobState>& job);

struct OffscreenJobResult
{
    std::shared_ptr<ImageResource>      outputImage        = nullptr;
    std::vector<std::shared_ptr<void>>  retainedResources;
};

enum class EOffscreenJobPhase : uint8_t
{
    Pending = 0,
    Queued,
    Recorded,
    GpuCompleted,
    Failed,
    Cancelled,
};

struct OffscreenJobState
{
    using CreateOutputFn = std::function<std::shared_ptr<ImageResource>(IRender* render)>;
    using ExecuteFn      = std::function<bool(ICommandBuffer* cmdBuf, ImageResource* output)>;

    std::string    debugName;
    CreateOutputFn createOutputFn;
    ExecuteFn      executeFn;

    std::shared_ptr<OffscreenJobResult> result     = std::make_shared<OffscreenJobResult>();
    EOffscreenJobPhase                  phase      = EOffscreenJobPhase::Pending;
    bool                                bCancelled = false;

    [[nodiscard]] bool isReadyToQueue() const
    {
        return executeFn && phase == EOffscreenJobPhase::Pending;
    }

    [[nodiscard]] bool isGpuCompleted() const { return phase == EOffscreenJobPhase::GpuCompleted; }
    [[nodiscard]] bool hasFailed() const { return phase == EOffscreenJobPhase::Failed; }
};

inline void finalizeSubmittedOffscreenJobs(std::vector<std::shared_ptr<OffscreenJobState>>& jobs)
{
    for (auto& job : jobs) {
        if (!job) {
            continue;
        }

        if (job->phase == EOffscreenJobPhase::Recorded) {
            job->phase = EOffscreenJobPhase::GpuCompleted;
        }
    }
    jobs.clear();
}


} // namespace ya
