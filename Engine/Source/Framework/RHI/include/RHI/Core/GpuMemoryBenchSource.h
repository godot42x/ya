#pragma once

#include "Core/Profiling/BenchAggregator.h"
#include "RHI/Core/GpuMemoryStats.h"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace ya
{

/// `gpu_memory` section of a benchmark summary: what the device's GPU memory
/// looks like, one sample per frame.
///
/// Series, MiB: one per resource class plus `totalMiB`. Facts: the peak each
/// class reached over the window, plus the per-heap budgets the device
/// reports. MiB rather than bytes because the windowed statistics hold
/// `float`, and a byte count in the hundreds of megabytes would lose its low
/// digits to rounding.
///
/// The source does not know what produces the numbers -- it takes a provider,
/// so the device wires it to itself and a test wires it to whatever it likes.
/// It lives next to `FGpuMemoryStats` (rather than with the other sources in
/// Core/Profiling) because that is the type it reports on, and Core may not
/// reach into the RHI.
class YA_RHI_API GpuMemoryBenchSource final : public profiling::IBenchSource
{
  public:
    using StatsProvider = std::function<FGpuMemoryStats()>;

  private:
    StatsProvider                              _provider;
    std::array<uint64_t, kGpuMemoryClassCount> _peakBytes{};
    std::vector<FGpuMemoryHeapBudget>          _heaps;
    bool                                       _bSupported = false;

  public:
    explicit GpuMemoryBenchSource(StatsProvider provider);

    void onWindowBegin() override;
    void sampleFrame(profiling::BenchSeriesWriter& out) override;
    void writeFacts(nlohmann::json& section) const override;
};

} // namespace ya
