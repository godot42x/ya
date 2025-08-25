
#pragma once
#include <Core/Base.h>

namespace ya
{
using clock_t = std::chrono::steady_clock;

struct TimeManager
{
    static TimeManager *get()
    {
        static TimeManager instance;
        return &instance;
    }

    auto now() -> clock_t::time_point { return clock_t::now(); }
};
} // namespace ya