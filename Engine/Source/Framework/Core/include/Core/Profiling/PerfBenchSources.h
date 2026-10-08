#pragma once

#include "Core/Memory/Allocator.h"
#include "Core/Profiling/BenchAggregator.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <unordered_map>

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

/// `gpu_passes` section: the render graph's per-pass GPU times of the frame
/// just completed. The backend owns the readback (its timestamp query pool);
/// this source only republishes what the owner's `provider` produced, keyed by
/// pass name. The provider callback keeps Core free of an RHI dependency
/// (ya-rhi already depends on Core); the frame owner supplies it.
class YA_CORE_API PassGpuTimeBenchSource final : public IBenchSource
{
public:
    using Provider = std::function<void(std::unordered_map<std::string, float>& outPassTimesMs)>;

    explicit PassGpuTimeBenchSource(Provider provider);

    void sampleFrame(BenchSeriesWriter& out) override;

private:
    Provider _provider;
};

/// `render_counters` section: the frame's GPU command counts (draws, dispatch,
/// binds, 2D batches) snapshotted and reset at frame end. Keys are the
/// counter struct's fields; only counters with a nonzero value appear, so a
/// section stays readable.
class YA_CORE_API RenderCounterBenchSource final : public IBenchSource
{
  public:
    RenderCounterBenchSource() = default;

    void sampleFrame(BenchSeriesWriter& out) override;
};

/// `process_memory` section: what the OS charged this process, one sample per
/// frame. MiB, for the same reason the GPU memory section uses MiB -- the
/// windowed statistics hold `float`.
///
/// This is the process half of the memory report; `gpu_memory` is the device
/// half. They are two different questions and their answers must not be added:
/// on unified-memory machines (Apple Silicon) the same physical pages can be
/// counted by both, which is what the `unifiedMemory` fact flags.
///
/// The heap series (bytes allocated and freed per frame, live bytes per owner)
/// come from the engine's allocator layer and are absent in builds that do not
/// count -- see `Core/Memory/Allocator.h`.
class YA_CORE_API ProcessMemoryBenchSource final : public IBenchSource
{
    memory::FAllocatorStats _previousAllocator;

  public:
    ProcessMemoryBenchSource() = default;

    void onWindowBegin() override;
    void sampleFrame(BenchSeriesWriter& out) override;
    /// `unifiedMemory`: whether the CPU and GPU share physical memory here.
    void writeFacts(nlohmann::json& section) const override;
};

} // namespace ya::profiling
