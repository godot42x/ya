#pragma once

#include "Core/Profiling/BenchAggregator.h"

#include <chrono>
#include <cstdint>

namespace ya
{
struct PerfState;
}

namespace ya::profiling
{

/// `frame` section: `frameMs` is the wall interval between consecutive
/// sampled frame ends (what 1 / FPS measures, including the event pump and any
/// present wait); `cpuMs` / `gpuMs` are the host tick's CPU time and the whole
/// frame's GPU timestamp as `PerfState` publishes them. A PerfState value not
/// rewritten since the previous sample is skipped, never counted twice.
class YA_CORE_API FrameBenchSource final : public IBenchSource
{
    using clock_t = std::chrono::steady_clock;

    const PerfState&    _perf;
    clock_t::time_point _windowStart{};
    clock_t::time_point _lastFrameEnd{};
    uint64_t            _frames          = 0;
    uint64_t            _lastCpuRevision = 0;
    uint64_t            _lastGpuRevision = 0;

  public:
    explicit FrameBenchSource(const PerfState& perf);

    void onWindowBegin() override;
    void sampleFrame(BenchSeriesWriter& out) override;
    /// `fps`: sampled frames over the window's wall time.
    void writeFacts(nlohmann::json& section) const override;
};

/// `cpu_stages` section: every `PerfState` CPU-time sample (`YA_PERF_SCOPE`)
/// written during the frame, keyed by sample name. A scope that runs several
/// times in one frame contributes its last run, which is what PerfState keeps.
class YA_CORE_API PerfStageBenchSource final : public IBenchSource
{
    const PerfState& _perf;
    uint64_t         _lastRevision = 0;

  public:
    explicit PerfStageBenchSource(const PerfState& perf);

    void onWindowBegin() override;
    void sampleFrame(BenchSeriesWriter& out) override;
};

} // namespace ya::profiling
