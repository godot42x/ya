#pragma once

#include "Foundation/Core/Api.h"

#include <functional>
#include <memory>

namespace ya
{

struct App;
struct IRender;
struct OffscreenJobState;

struct OffscreenJobQueueService
{
    std::function<void(const std::shared_ptr<OffscreenJobState>&, std::function<void(ICommandBuffer*)>)> enqueue;
};

YA_HOST_API void queueOffscreenJob(App* app, IRender* render, const std::shared_ptr<OffscreenJobState>& job);
YA_HOST_API void queueOffscreenJob(const OffscreenJobQueueService& queueService, IRender* render, const std::shared_ptr<OffscreenJobState>& job);
YA_HOST_API void cancelOffscreenJob(std::shared_ptr<OffscreenJobState>& job);

} // namespace ya
