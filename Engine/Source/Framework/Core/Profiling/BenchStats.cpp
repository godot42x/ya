#include "Core/Profiling/BenchStats.h"

#include <algorithm>
#include <cmath>

namespace ya::profiling
{

double percentileOfSorted(std::span<const float> sorted, double fraction)
{
    if (sorted.empty()) {
        return 0.0;
    }
    const double rank  = std::clamp(fraction, 0.0, 1.0) * static_cast<double>(sorted.size() - 1);
    const auto   lower = static_cast<size_t>(std::floor(rank));
    const size_t upper = std::min(lower + 1, sorted.size() - 1);
    const double t     = rank - static_cast<double>(lower);
    return static_cast<double>(sorted[lower]) + (static_cast<double>(sorted[upper]) - static_cast<double>(sorted[lower])) * t;
}

FBenchWindowStats computeBenchWindowStats(std::vector<float> samples)
{
    FBenchWindowStats stats;
    stats.count = samples.size();
    if (samples.empty()) {
        return stats;
    }

    std::sort(samples.begin(), samples.end());
    double sum = 0.0;
    for (const float value : samples) {
        sum += static_cast<double>(value);
    }
    stats.mean = sum / static_cast<double>(samples.size());
    stats.min  = samples.front();
    stats.max  = samples.back();
    stats.p50  = percentileOfSorted(samples, 0.50);
    stats.p95  = percentileOfSorted(samples, 0.95);
    stats.p99  = percentileOfSorted(samples, 0.99);
    return stats;
}

} // namespace ya::profiling
