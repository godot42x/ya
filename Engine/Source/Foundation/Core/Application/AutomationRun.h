#pragma once

#include "Core/Api.h"

#include <cstdint>

namespace ya
{

struct YA_CORE_API AppAutomationRunOptions
{
    uint64_t exitAfterFrame = 0;
    uint16_t controlPort    = 0;
};

YA_CORE_API void applyAutomationRunArgs(int argc, char** argv, AppAutomationRunOptions& outOptions);

[[nodiscard]] YA_CORE_API bool shouldAutomationExitAfterFrame(uint64_t completedFrameCount,
                                                              uint64_t exitAfterFrame);

} // namespace ya
