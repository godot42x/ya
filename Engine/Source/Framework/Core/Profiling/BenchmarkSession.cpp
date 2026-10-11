#include "Core/Profiling/BenchmarkSession.h"

#include "Core/Log.h"
#include "Core/Profiling/BenchSummary.h"
#include "Core/Profiling/PerfState.h"

#include <filesystem>

namespace ya::profiling
{

BenchmarkSession::BenchmarkSession()
    : _frame(PerfState::get()),
      _stages(PerfState::get()),
      _passes([this](std::unordered_map<std::string, float>& out) {
          if (_host) {
              _host->completedPassGpuTimes(out);
          }
      })
{
}

BenchmarkSession::~BenchmarkSession()
{
    if (_bTimingOverridden) {
        setPassGpuTimingEnabled(_bSavedPassGpuTiming);
    }
}

bool BenchmarkSession::begin(IBenchmarkHost& host, const FBenchmarkCommonConfig& config, const FBenchmarkRunFacts& facts)
{
    _host       = &host;
    _config     = config;
    _facts      = facts;
    _phase      = EPhase::Warmup;
    _frameIndex = 0;
    _bFailed    = false;
    _bSummaryWriteFailed = false;

    _present = host.selectPresentMode(_config.presentMode);
    if (!_present.bOk) {
        _bFailed = true;
        _host    = nullptr;
        return false;
    }
    if (_present.selected == "fifo") {
        YA_CORE_WARN("Benchmark: the surface offers no non-vsync present mode; frame time is bound to the display refresh");
    }

    host.matchResolution(_config.width, _config.height);

    _bSavedPassGpuTiming = isPassGpuTimingEnabled();
    setPassGpuTimingEnabled(true);
    _bTimingOverridden = true;

    _frameRegistration         = registerBenchSource("frame", _frame);
    _stageRegistration         = registerBenchSource("cpu_stages", _stages);
    _passRegistration          = registerBenchSource("gpu_passes", _passes);
    _counterRegistration       = registerBenchSource("render_counters", _counters);
    _processMemoryRegistration = registerBenchSource("process_memory", _processMemory);

    _summaryPath = (std::filesystem::path(_config.outputDir) / "bench_summary.json").string();
    YA_CORE_INFO("Benchmark '{}' started: {}x{}, present {} (requested {}), warmup {} + sample {} frames, scene '{}', fixed dt {}, camera keyframes {}",
                 _config.name,
                 _config.width,
                 _config.height,
                 _present.selected,
                 _config.presentMode,
                 _config.warmupFrames,
                 _config.sampleFrames,
                 _facts.scene,
                 _facts.fixedDeltaSeconds,
                 _facts.cameraKeyframes);

    if (_config.warmupFrames == 0) {
        openWindow();
    }
    return true;
}

void BenchmarkSession::onFrameEnd()
{
    if (_phase == EPhase::Finished || _bFailed) {
        return;
    }
    ++_frameIndex;

    if (_phase == EPhase::Warmup) {
        if (_frameIndex >= _config.warmupFrames) {
            openWindow();
        }
        return;
    }

    auto& aggregator = benchAggregator();
    aggregator.sampleFrame();
    if (aggregator.getSampledFrameCount() >= _config.sampleFrames) {
        finish();
    }
}

void BenchmarkSession::openWindow()
{
    YA_CORE_INFO("Benchmark '{}': warmup done after {} frames, sampling {}", _config.name, _frameIndex, _config.sampleFrames);
    benchAggregator().beginWindow(static_cast<size_t>(_config.sampleFrames));
    _phase = EPhase::Sampling;
}

void BenchmarkSession::finish()
{
    auto& aggregator = benchAggregator();
    aggregator.endWindow();
    _phase = EPhase::Finished;

    FBenchmarkDeviceMeta device;
    if (_host) {
        _host->fillDeviceMeta(device);
    }
    if (device.presentMode.empty()) {
        device.presentMode = _present.selected;
    }

    const FBenchSummaryMeta meta{
        .name                  = _config.name,
        .configPath            = _config.sourcePath,
        .project               = _facts.project,
        .scene                 = _facts.scene,
        .host                  = collectBenchHostFacts(),
        .graphicsApi           = device.graphicsApi,
        .gpuName               = device.gpuName,
        .gpuDriver             = device.gpuDriver,
        .renderWidth           = _config.width,
        .renderHeight          = _config.height,
        .swapchainWidth        = device.swapchainWidth,
        .swapchainHeight       = device.swapchainHeight,
        .requestedPresentMode  = _config.presentMode,
        .presentMode           = device.presentMode,
        .availablePresentModes = _present.available,
        .bFrameRateCapped      = _facts.bFrameRateCapped,
        .fixedDeltaSeconds     = _facts.fixedDeltaSeconds,
        .bCpuTrace             = _facts.bCpuTrace,
        .warmupFrames          = _config.warmupFrames,
        .sampleFrames          = _config.sampleFrames,
        .sampledFrames         = aggregator.getSampledFrameCount(),
        .cameraKeyframes       = _facts.cameraKeyframes,
    };

    const nlohmann::json summary = buildBenchSummaryJson(meta, aggregator);
    if (writeBenchSummaryFile(_summaryPath, summary)) {
        const nlohmann::json frameMs = summary.value(nlohmann::json::json_pointer("/frame/frameMs"), nlohmann::json::object());
        YA_CORE_INFO("Benchmark '{}' done: {} frames, frame p50 {:.3f} ms p99 {:.3f} ms, present {}, summary {}",
                     _config.name,
                     meta.sampledFrames,
                     frameMs.value("p50", 0.0),
                     frameMs.value("p99", 0.0),
                     meta.presentMode,
                     _summaryPath);
    }
    else {
        _bFailed             = true;
        _bSummaryWriteFailed = true;
        YA_CORE_ERROR("Benchmark '{}': failed to write {}", _config.name, _summaryPath);
    }

    _frameRegistration.reset();
    _stageRegistration.reset();
    _passRegistration.reset();
    _counterRegistration.reset();
    _processMemoryRegistration.reset();
    if (_bTimingOverridden) {
        setPassGpuTimingEnabled(_bSavedPassGpuTiming);
        _bTimingOverridden = false;
    }
    if (_host) {
        _host->requestQuit();
        _host = nullptr;
    }
}

} // namespace ya::profiling
