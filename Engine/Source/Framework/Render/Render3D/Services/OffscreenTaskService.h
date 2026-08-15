#pragma once

#include "Core/Base.h"

#include "RHI/Core/OffscreenJob.h"

#include <memory>
#include <vector>

namespace ya
{

struct IOffscreenTaskScheduler;
struct IRender;
struct ICommandBuffer;

struct YA_RENDER_3D_API OffscreenTaskService
{
    void init(IRender* render);
    void shutdown();
    void tick(IOffscreenTaskScheduler& scheduler);

    [[nodiscard]] bool isPending() const { return _pending; }

  private:
    void finalizeCompletedJobs();

    IRender*                                        _render = nullptr;
    stdptr<ICommandBuffer>                          _commandBuffer;
    void*                                           _fence   = nullptr;
    bool                                            _pending = false;
    std::vector<std::shared_ptr<OffscreenJobState>> _submittedJobs;
};

} // namespace ya
