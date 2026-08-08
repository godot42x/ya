#pragma once

#include "Core/Api.h"
#include "RHI/Core/OffscreenJob.h"

namespace ya
{

struct App;
struct IRender;

YA_HOST_API void queueOffscreenJob(App* app, IRender* render, const std::shared_ptr<OffscreenJobState>& job);

} // namespace ya
