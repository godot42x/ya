
#pragma once
#include <chrono>

namespace ya
{
using clock_t = std::chrono::steady_clock;

struct ClockManager
{

    auto now() -> clock_t::time_point { return clock_t::now(); }
    auto nowString() -> std::string
    {
        auto now_ = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now_);
        auto ms   = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now_.time_since_epoch()) %
                  1000;

        std::tm tm;
#ifdef _WIN32
        localtime_s(&tm, &time);
#else
        localtime_r(&time, &tm);
#endif

        return std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}",
                           tm.tm_year + 1900,
                           tm.tm_mon + 1,
                           tm.tm_mday,
                           tm.tm_hour,
                           tm.tm_min,
                           tm.tm_sec,
                           ms.count());
    }
};
} // namespace ya