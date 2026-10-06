#pragma once

#include "Core/Api.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace ya::profiling
{

using BenchSeriesMap = std::map<std::string, std::vector<float>, std::less<>>;

/// What a source writes into on each sampled frame: one value per key per
/// frame. A key a frame does not record simply has fewer samples, and its
/// `count` in the summary says so.
class BenchAggregator;

class YA_CORE_API BenchSeriesWriter
{
    BenchAggregator* _aggregator      = nullptr;
    std::string      _section;
    size_t           _reserveCapacity = 0;

  public:
    BenchSeriesWriter(BenchAggregator& aggregator, std::string section, size_t reserveCapacity);

    void record(std::string_view key, float value);
};

/// One statistics source of a benchmark summary. A source owns one summary
/// section (`frame`, `cpu_stages`, `gpu_passes`, `gpu_memory`, ...); the
/// aggregator owns the samples and turns each key into window statistics.
struct IBenchSource
{
    virtual ~IBenchSource() = default;

    /// The sampling window opens (warmup is over): drop anything remembered
    /// from frames before it. Called without the aggregator lock.
    virtual void onWindowBegin() {}
    /// One sampled frame, on the frame thread after the frame completed.
    /// Called without the aggregator lock; may re-enter the aggregator.
    virtual void sampleFrame(BenchSeriesWriter& out) = 0;
    /// Non-windowed facts merged into the section once the window closes
    /// (peaks, budgets, labels). Keys must not collide with recorded series.
    /// Called without the aggregator lock; may re-enter the aggregator.
    virtual void writeFacts(nlohmann::json& section) const { (void)section; }
};

/// Collects benchmark sections from registered sources over one window.
///
/// Sources register for the whole process lifetime they can answer for (the
/// owner of the data registers next to the data) and cost nothing outside a
/// window: `sampleFrame` only reaches them between `beginWindow` and
/// `endWindow`. Several sources may share a section as long as their keys do
/// not overlap.
///
/// The mutex is not recursive. `beginWindow`, `sampleFrame` and
/// `buildSections` copy the source list, drop the lock, then call
/// `onWindowBegin` / `sampleFrame` / `writeFacts`, so those callbacks may
/// re-enter the aggregator. The list is a snapshot: a source registered
/// during a callback is not invoked until the next call, and every source in
/// the snapshot must stay alive until the calls return. `record` takes the
/// lock per sample.
class YA_CORE_API BenchAggregator
{
  public:
    using SourceId = uint32_t;

  private:
    struct FSourceSlot
    {
        SourceId      id = 0;
        std::string   section;
        IBenchSource* source = nullptr;
    };

    mutable std::mutex                                _mutex;
    std::vector<FSourceSlot>                          _sources;
    std::map<std::string, BenchSeriesMap, std::less<>> _sections;
    SourceId                                          _nextId         = 1;
    size_t                                            _expectedFrames = 0;
    uint64_t                                          _sampledFrames  = 0;
    bool                                              _bWindowOpen    = false;

    friend class BenchSeriesWriter;
    void appendSample(std::string_view section, std::string_view key, float value, size_t reserveCapacity);

  public:
    [[nodiscard]] SourceId addSource(std::string section, IBenchSource& source);
    bool                   removeSource(SourceId id);
    [[nodiscard]] size_t   getSourceCount() const;

    /// Clears the previous window's samples. `expectedFrames` only sizes the
    /// sample storage so recording does not allocate mid-window.
    void beginWindow(size_t expectedFrames);
    void sampleFrame();
    void endWindow();

    [[nodiscard]] bool     isWindowOpen() const;
    [[nodiscard]] uint64_t getSampledFrameCount() const;

    /// `{section: {key: {count, mean, min, p50, p95, p99, max}, ...facts}}`.
    [[nodiscard]] nlohmann::json buildSections() const;
};

/// Keeps one source registered for its own lifetime.
class YA_CORE_API BenchSourceRegistration
{
    BenchAggregator*          _aggregator = nullptr;
    BenchAggregator::SourceId _id         = 0;

  public:
    BenchSourceRegistration() = default;
    BenchSourceRegistration(BenchAggregator& aggregator, std::string section, IBenchSource& source);
    ~BenchSourceRegistration();

    BenchSourceRegistration(const BenchSourceRegistration&)            = delete;
    BenchSourceRegistration& operator=(const BenchSourceRegistration&) = delete;
    BenchSourceRegistration(BenchSourceRegistration&& other) noexcept;
    BenchSourceRegistration& operator=(BenchSourceRegistration&& other) noexcept;

    void               reset();
    [[nodiscard]] bool isRegistered() const { return _aggregator != nullptr; }
};

/// The process-wide aggregator the benchmark run samples. Data owners register
/// their sources here: `registerBenchSource("gpu_passes", _passSource)`.
[[nodiscard]] YA_CORE_API BenchAggregator& benchAggregator();
[[nodiscard]] YA_CORE_API BenchSourceRegistration registerBenchSource(std::string section, IBenchSource& source);

} // namespace ya::profiling
