#pragma once

#include "Core/Api.h"

#include <cstdint>

namespace ya
{

struct YA_APP_SERVICES_API AppAutomationRunOptions
{
    uint64_t exitAfterFrame = 0;
};

YA_APP_SERVICES_API void applyAutomationRunArgs(int argc, char** argv, AppAutomationRunOptions& outOptions);

[[nodiscard]] YA_APP_SERVICES_API bool shouldAutomationExitAfterFrame(uint64_t completedFrameCount,
                                                                      uint64_t exitAfterFrame);

} // namespace ya
