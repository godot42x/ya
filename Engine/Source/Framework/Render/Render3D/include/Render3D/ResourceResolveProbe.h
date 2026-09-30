#pragma once

#include <cstdint>

namespace ya
{

/// How many component views a resource-prepare pass walked. Steady state is
/// zero: discovery is the scene edit funnel plus one seed, and completions
/// re-enter through callbacks. Tests assert the counter does not grow with
/// entity count once the seed has run.
inline uint64_t& resourceResolveComponentTouches()
{
    static uint64_t touches = 0;
    return touches;
}

inline void noteResourceResolveView()
{
    ++resourceResolveComponentTouches();
}

} // namespace ya
