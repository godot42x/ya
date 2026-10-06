#pragma once

#include "Core/Api.h"

#include <cstddef>
#include <span>
#include <vector>

namespace ya::profiling
{

/// Statistics of one series over a benchmark window.
///
/// Percentiles interpolate linearly between the two closest ranks of the
/// sorted window (`rank = q * (n - 1)`, numpy's default), so the p50 of an
/// even-length window is the midpoint of its two middle samples. The window is
/// kept as full samples rather than a histogram: a run is hundreds to a few
/// thousand frames per series, which is kilobytes, and exact ranks keep
/// p99 / max honest at that size.
struct FBenchWindowStats
{
    size_t count = 0;
    double mean  = 0.0;
    double min   = 0.0;
    double p50   = 0.0;
    double p95   = 0.0;
    double p99   = 0.0;
    double max   = 0.0;
};

/// `sorted` ascending; `fraction` in [0, 1]. 0 for an empty window.
[[nodiscard]] YA_CORE_API double percentileOfSorted(std::span<const float> sorted, double fraction);

[[nodiscard]] YA_CORE_API FBenchWindowStats computeBenchWindowStats(std::vector<float> samples);

} // namespace ya::profiling
