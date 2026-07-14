#pragma once

#include "Render/Core/RenderImage.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ya
{

struct IRender;
struct ICommandBuffer;

struct OffscreenJobResult
{
    std::shared_ptr<RenderImage>        outputImage        = nullptr;
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
    using CreateOutputFn = std::function<std::shared_ptr<RenderImage>(IRender* render)>;
    using ExecuteFn      = std::function<bool(ICommandBuffer* cmdBuf, RenderImage* output)>;

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
