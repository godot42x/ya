#pragma once

#include <cstdint>

namespace ya
{

struct RenderRuntimeClockState
{
    uint32_t frameIndex    = 0;
    uint64_t elapsedTimeMS = 0;
};

} // namespace ya
