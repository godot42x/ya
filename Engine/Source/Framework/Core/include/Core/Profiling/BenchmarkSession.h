#pragma once

// One benchmark window shared by every host. The session does not include
// RHI: present mode, the drawable size, and the device are strings and
// numbers the host fills in.

#include "Core/Api.h"
#include "Core/Profiling/BenchmarkConfig.h"
#include "Core/Profiling/PerfBenchSources.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ya::profiling
{

struct FBenchmarkPresentModeSelection
{
    std::string              selected;
    std::vector<std::string> available;
    /// False when the host cannot present at all (no swapchain).
    bool                     bOk = true;
};

struct FBenchmarkDeviceMeta
{
    std::string graphicsApi;
    std::string gpuName;
    std::string gpuDriver;
    uint32_t    swapchainWidth  = 0;
    uint32_t    swapchainHeight = 0;
    /// The mode actually in use when the window closed, which can differ
    /// from the mode selected at the start if a later recreate changed it.
    std::string presentMode;
};

/// Fields of `bench_summary.json` `meta` that are the host's run, not the
/// device. `scene` is the game scene path, or the GUI page name.
struct FBenchmarkRunFacts
{
    std::string project;
    std::string scene;
    bool        bFrameRateCapped  = false;
    float       fixedDeltaSeconds = 0.0f;
    bool        bCpuTrace         = false;
    size_t      cameraKeyframes   = 0;
};

class YA_CORE_API IBenchmarkHost
{
  public:
    virtual ~IBenchmarkHost() = default;

    [[nodiscard]] virtual FBenchmarkPresentModeSelection selectPresentMode(std::string_view requestedLabel) = 0;
    virtual void                                         matchResolution(uint32_t width, uint32_t height)   = 0;
    virtual void                                         fillDeviceMeta(FBenchmarkDeviceMeta& meta)         = 0;
    virtual void completedPassGpuTimes(std::unordered_map<std::string, float>& outPassTimesMs)              = 0;
    virtual void requestQuit()                                                                              = 0;
};

/// Warmup, sample, then write `outputDir/bench_summary.json` and ask the
/// host to quit. Registers `frame`, `cpu_stages`, `gpu_passes`,
/// `render_counters`, and `process_memory`. The device registers
/// `gpu_memory` itself.
class YA_CORE_API BenchmarkSession
{
  public:
    enum class EPhase : uint8_t
    {
        Warmup,
        Sampling,
        Finished,
    };

  private:
    FrameBenchSource         _frame;
    PerfStageBenchSource     _stages;
    PassGpuTimeBenchSource   _passes;
    RenderCounterBenchSource _counters;
    ProcessMemoryBenchSource _processMemory;
    BenchSourceRegistration  _frameRegistration;
    BenchSourceRegistration  _stageRegistration;
    BenchSourceRegistration  _passRegistration;
    BenchSourceRegistration  _counterRegistration;
    BenchSourceRegistration  _processMemoryRegistration;
    IBenchmarkHost*          _host = nullptr;
    FBenchmarkCommonConfig   _config;
    FBenchmarkRunFacts       _facts;
    FBenchmarkPresentModeSelection _present;
    bool                     _bSavedPassGpuTiming = false;
    bool                     _bTimingOverridden   = false;
    EPhase                   _phase               = EPhase::Warmup;
    uint64_t                 _frameIndex          = 0;
    std::string              _summaryPath;
    bool                     _bFailed             = false;
    bool                     _bSummaryWriteFailed = false;

  public:
    BenchmarkSession();
    ~BenchmarkSession();

    BenchmarkSession(const BenchmarkSession&)            = delete;
    BenchmarkSession& operator=(const BenchmarkSession&) = delete;

    /// False when the host cannot present. Call `setFacts` again before
    /// `onFrameEnd` if a fact (the frame-rate cap) changes during the run.
    [[nodiscard]] bool begin(IBenchmarkHost& host, const FBenchmarkCommonConfig& config, const FBenchmarkRunFacts& facts);
    void               setFacts(const FBenchmarkRunFacts& facts) { _facts = facts; }
    void               onFrameEnd();

    [[nodiscard]] EPhase             phase() const { return _phase; }
    [[nodiscard]] uint64_t           frameIndex() const { return _frameIndex; }
    [[nodiscard]] const std::string& summaryPath() const { return _summaryPath; }
    [[nodiscard]] bool               failed() const { return _bFailed; }
    [[nodiscard]] bool               summaryWriteFailed() const { return _bSummaryWriteFailed; }

  private:
    void openWindow();
    void finish();
};

} // namespace ya::profiling
