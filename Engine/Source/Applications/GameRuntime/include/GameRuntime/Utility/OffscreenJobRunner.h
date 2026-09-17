#pragma once

#include "Core/Api.h"
#include "RHI/Core/OffscreenJob.h"

namespace ya
{

struct App;
struct IRender;

YA_GAME_RUNTIME_API void queueOffscreenJob(App* app, IRender* render, const std::shared_ptr<OffscreenJobState>& job);

} // namespace ya
