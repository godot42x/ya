#pragma once

#include <cstdint>

namespace ya
{

struct HostClockState
{
    uint32_t hostTick      = 0;
    uint64_t elapsedTimeMS = 0;
};

} // namespace ya
