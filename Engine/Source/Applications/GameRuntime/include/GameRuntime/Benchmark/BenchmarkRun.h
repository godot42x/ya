#pragma once

#include "Core/Base.h"
#include "Core/Profiling/BenchmarkSession.h"
#include "GameRuntime/Benchmark/BenchmarkConfig.h"

#include <string>

namespace ya
{

struct App;
struct INativeWindow;

/// What the app knows about the run that the benchmark cannot look up itself.
struct FBenchmarkRunContext
{
    std::string    project;
    std::string    scene;
    INativeWindow* mainWindow = nullptr;
};

/// Game-runtime adapter around `profiling::BenchmarkSession`. This class keeps
/// the camera path, the fixed step, the frame-rate cap, and the render
/// resolution. The warmup / sample / summary state machine lives in the session.
class YA_GAME_RUNTIME_API BenchmarkRun : private profiling::IBenchmarkHost
{
    BenchmarkConfig                          _config;
    FBenchmarkRunContext                     _context;
    profiling::BenchmarkSession              _session;
    profiling::FBenchmarkRunFacts            _facts;
    App*                                     _app = nullptr;

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

    [[nodiscard]] profiling::BenchmarkSession::EPhase phase() const { return _session.phase(); }
    [[nodiscard]] uint64_t                            frameIndex() const { return _session.frameIndex(); }
    [[nodiscard]] const std::string&                  summaryPath() const { return _session.summaryPath(); }
    [[nodiscard]] bool                                didSummaryWriteFail() const { return _session.summaryWriteFailed(); }

  private:
    [[nodiscard]] profiling::FBenchmarkPresentModeSelection selectPresentMode(std::string_view requestedLabel) override;
    void                                                    matchResolution(uint32_t width, uint32_t height) override;
    void                                                    fillDeviceMeta(profiling::FBenchmarkDeviceMeta& meta) override;
    void completedPassGpuTimes(std::unordered_map<std::string, float>& outPassTimesMs) override;
    void                                                    requestQuit() override;

    [[nodiscard]] profiling::FBenchmarkRunFacts currentFacts() const;
};

} // namespace ya
