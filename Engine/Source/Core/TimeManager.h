
#pragma once
#include <chrono>

namespace ya
{
using clock_t = std::chrono::steady_clock;

struct TimeManager
{
    static TimeManager *get();

    auto now() -> clock_t::time_point { return clock_t::now(); }
};
} // namespace ya