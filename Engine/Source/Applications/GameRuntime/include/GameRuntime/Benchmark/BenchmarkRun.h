#pragma once

#include "Core/Base.h"
#include "Core/Profiling/BenchAggregator.h"
#include "Core/Profiling/PerfBenchSources.h"
#include "GameRuntime/Benchmark/BenchmarkConfig.h"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ya
{

struct App;
struct INativeWindow;
struct IRender;

/// What the app knows about the run that the benchmark cannot look up itself.
struct FBenchmarkRunContext
{
    std::string    project;
    std::string    scene;
    INativeWindow* mainWindow = nullptr;
};

/// One `--benchmark` run of the game: forces the presentation conditions,
/// drives the camera path by frame index, warms up, samples a window through
/// `profiling::benchAggregator()`, writes `bench_summary.json` and asks the
/// app to quit. Frame-indexed throughout -- nothing here reads wall time
/// except the frame-interval sample itself.
class YA_GAME_RUNTIME_API BenchmarkRun
{
  public:
    enum class EPhase : uint8_t
    {
        Warmup,
        Sampling,
        Finished,
    };

  private:
    BenchmarkConfig                    _config;
    FBenchmarkRunContext               _context;
    profiling::FrameBenchSource        _frameSource;
    profiling::PerfStageBenchSource    _stageSource;
    /// Pass times come from the render device's readback, behind GameRuntime's
    /// RHI dependency (the Core source cannot reach IRender itself).
    std::function<void(std::unordered_map<std::string, float>&)> _passTimeProvider;
    profiling::PassGpuTimeBenchSource   _passSource;
    profiling::RenderCounterBenchSource _counterSource;
    /// Process memory is the OS's, not the device's: it is sampled straight
    /// from the platform, with no provider to inject.
    profiling::ProcessMemoryBenchSource _processMemorySource;
    profiling::BenchSourceRegistration  _frameRegistration;
    profiling::BenchSourceRegistration  _stageRegistration;
    profiling::BenchSourceRegistration  _passRegistration;
    profiling::BenchSourceRegistration  _counterRegistration;
    profiling::BenchSourceRegistration  _processMemoryRegistration;
    /// The pass-timing toggle found on begin, restored on finish: the timestamp
    /// brackets cost query pool memory, so only the sampling owns them.
    bool                               _bSavedPassGpuTiming = false;
    /// The render device, resolved on begin; the pass-time provider reads
    /// through it. Raw pointer: the device outlives the run (App owns both).
    IRender*                           _renderWeak = nullptr;
    EPhase                             _phase      = EPhase::Warmup;
    uint64_t                           _frameIndex = 0;
    std::vector<EPresentMode::T>       _availablePresentModes;
    std::string                        _summaryPath;
    bool                               _bSummaryWriteFailed = false;

  public:
    explicit BenchmarkRun(BenchmarkConfig config);

    /// Before the first frame, after the device and the startup scene exist.
    void begin(App& app, FBenchmarkRunContext context);
    /// The step the frame's logic advances by.
    [[nodiscard]] float frameDelta(float wallDeltaSeconds) const;
    /// Between logic and render: pose the scene camera for this frame.
    void applyCameraPath(App& app) const;
    /// After the frame's tick completed (its perf scopes closed).
    void onFrameEnd(App& app);

    [[nodiscard]] EPhase             getPhase() const { return _phase; }
    [[nodiscard]] uint64_t           getFrameIndex() const { return _frameIndex; }
    [[nodiscard]] const std::string& getSummaryPath() const { return _summaryPath; }
    [[nodiscard]] bool               didSummaryWriteFail() const { return _bSummaryWriteFailed; }

  private:
    void openWindow(App& app);
    void finish(App& app);
};

} // namespace ya
