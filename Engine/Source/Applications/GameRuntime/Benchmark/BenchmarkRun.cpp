#include "GameRuntime/Benchmark/BenchmarkRun.h"

#include "Core/Log.h"
#include "Core/Profiling/BenchSummary.h"
#include "Core/Profiling/PerfState.h"
#include "ECS/Entity.h"
#include "GameRuntime/App.h"
#include "GameRuntime/Lifecycle/FPSCtrl.h"
#include "GameRuntime/Render/SceneCameraQuery.h"
#include "RHI/Core/RenderSurfaceContext.h"
#include "RHI/Core/Swapchain.h"
#include "RHI/NativeWindow.h"
#include "RHI/Render.h"
#include "Scene3D/TransformComponent.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace ya
{

namespace
{

ISwapchain* hostSwapchain(App& app)
{
    IRenderSurfaceContext* surface = app.getRenderServices().getHostSurface();
    return surface ? surface->getSwapchain() : nullptr;
}

void matchDrawableToResolution(INativeWindow& window, uint32_t width, uint32_t height)
{
    int windowW = 0, windowH = 0, drawableW = 0, drawableH = 0;
    window.getWindowSize(windowW, windowH);
    window.getDrawableSize(drawableW, drawableH);
    if (drawableW == static_cast<int>(width) && drawableH == static_cast<int>(height)) {
        return;
    }
    // The window is sized in its own coordinate space; on a high-density
    // display that is points, so divide the pixel target by the density.
    const float density = windowW > 0 && drawableW > 0 ? static_cast<float>(drawableW) / static_cast<float>(windowW) : 1.0f;
    const int   targetW = std::max(1, static_cast<int>(std::lround(static_cast<float>(width) / density)));
    const int   targetH = std::max(1, static_cast<int>(std::lround(static_cast<float>(height) / density)));
    if (!window.setWindowSize(targetW, targetH)) {
        YA_CORE_WARN("Benchmark: could not resize the window to {}x{} (drawable stays {}x{})", targetW, targetH, drawableW, drawableH);
    }
}

} // namespace

BenchmarkRun::BenchmarkRun(BenchmarkConfig config)
    : _config(std::move(config)),
      _frameSource(PerfState::get()),
      _stageSource(PerfState::get()),
      _passSource([this](std::unordered_map<std::string, float>& out) {
          if (IRender* render = _renderWeak) {
              render->getCompletedPassGpuTimesMs(out);
          }
      })
{
}

void BenchmarkRun::begin(App& app, FBenchmarkRunContext context)
{
    _context    = std::move(context);
    _phase      = EPhase::Warmup;
    _frameIndex = 0;

    if (_config.bUncapFrameRate) {
        FPSControl::get()->bEnable = false;
    }

    EPresentMode::T chosen = EPresentMode::FIFO;
    if (ISwapchain* swapchain = hostSwapchain(app)) {
        _availablePresentModes = swapchain->getAvailablePresentModes();
        chosen                 = chooseBenchmarkPresentMode(_config.presentMode, _availablePresentModes);
        if (chosen != swapchain->getPresentMode()) {
            swapchain->setPresentMode(chosen);
        }
    }
    else {
        YA_CORE_WARN("Benchmark: no host swapchain; present mode left as is");
    }
    if (chosen == EPresentMode::FIFO) {
        YA_CORE_WARN("Benchmark: the surface offers no non-vsync present mode; frame time is bound to the display refresh");
    }

    if (_context.mainWindow) {
        matchDrawableToResolution(*_context.mainWindow, _config.width, _config.height);
    }
    app.getRenderServices().setRenderResolution(Extent2D{.width = _config.width, .height = _config.height});

    // Pass timestamps and counters belong to the sampling window only: the
    // brackets cost query-pool memory and two timestamp writes per pass, so
    // warmup runs without them.
    _bSavedPassGpuTiming = profiling::isPassGpuTimingEnabled();
    profiling::setPassGpuTimingEnabled(true);
    _renderWeak = app.getRenderServices().getRender();

    _frameRegistration          = profiling::registerBenchSource("frame", _frameSource);
    _stageRegistration          = profiling::registerBenchSource("cpu_stages", _stageSource);
    _passRegistration           = profiling::registerBenchSource("gpu_passes", _passSource);
    _counterRegistration        = profiling::registerBenchSource("render_counters", _counterSource);
    _processMemoryRegistration  = profiling::registerBenchSource("process_memory", _processMemorySource);

    _summaryPath = (std::filesystem::path(_config.outputDir) / "bench_summary.json").string();
    YA_CORE_INFO("Benchmark '{}' started: {}x{}, present {} (requested {}), warmup {} + sample {} frames, fixed dt {}, camera keyframes {}",
                 _config.name,
                 _config.width,
                 _config.height,
                 toBenchmarkPresentModeLabel(chosen),
                 toBenchmarkPresentModeLabel(_config.presentMode),
                 _config.warmupFrames,
                 _config.sampleFrames,
                 _config.fixedDeltaSeconds,
                 _config.cameraPath.keyframes.size());

    if (_config.warmupFrames == 0) {
        openWindow(app);
    }
}

float BenchmarkRun::frameDelta(float wallDeltaSeconds) const
{
    return _config.fixedDeltaSeconds > 0.0f ? _config.fixedDeltaSeconds : wallDeltaSeconds;
}

void BenchmarkRun::applyCameraPath(App& app) const
{
    const std::optional<FBenchmarkCameraPose> pose = sampleBenchmarkCameraPath(_config.cameraPath, _frameIndex);
    if (!pose) {
        return;
    }
    Entity* camera = app.getSceneServices().getPrimaryCamera();
    if (!camera || !camera->hasComponent<TransformComponent>()) {
        return;
    }
    auto* transform = camera->getComponent<TransformComponent>();
    transform->setPosition(pose->position);
    transform->setRotation(pose->rotation);
}

void BenchmarkRun::onFrameEnd(App& app)
{
    if (_phase == EPhase::Finished) {
        return;
    }
    ++_frameIndex;

    if (_phase == EPhase::Warmup) {
        if (_frameIndex >= _config.warmupFrames) {
            openWindow(app);
        }
        return;
    }

    auto& aggregator = profiling::benchAggregator();
    aggregator.sampleFrame();
    if (aggregator.getSampledFrameCount() >= _config.sampleFrames) {
        finish(app);
    }
}

void BenchmarkRun::openWindow(App& app)
{
    (void)app;
    // Before the window opens: the first frameMs sample starts at beginWindow,
    // and this line would otherwise sit inside that interval.
    YA_CORE_INFO("Benchmark '{}': warmup done after {} frames, sampling {}", _config.name, _frameIndex, _config.sampleFrames);
    profiling::benchAggregator().beginWindow(static_cast<size_t>(_config.sampleFrames));
    _phase = EPhase::Sampling;
}

void BenchmarkRun::finish(App& app)
{
    auto& aggregator = profiling::benchAggregator();
    aggregator.endWindow();
    _phase = EPhase::Finished;

    profiling::FBenchSummaryMeta meta{
        .name                 = _config.name,
        .configPath           = _config.sourcePath,
        .project              = _context.project,
        .scene                = _context.scene,
        .host                 = profiling::collectBenchHostFacts(),
        .renderWidth          = _config.width,
        .renderHeight         = _config.height,
        .requestedPresentMode = toBenchmarkPresentModeLabel(_config.presentMode),
        .bFrameRateCapped     = FPSControl::get()->bEnable,
        .fixedDeltaSeconds    = _config.fixedDeltaSeconds,
        .bCpuTrace            = app.getDesc().profiling.bCpuProfileEnabled,
        .warmupFrames         = _config.warmupFrames,
        .sampleFrames         = _config.sampleFrames,
        .sampledFrames        = aggregator.getSampledFrameCount(),
        .cameraKeyframes      = _config.cameraPath.keyframes.size(),
    };
    if (IRender* render = app.getRenderServices().getRender()) {
        const RenderDeviceInfo device = render->getDeviceInfo();
        meta.graphicsApi              = device.apiVersion;
        meta.gpuName                  = device.name;
        meta.gpuDriver                = device.driver;
    }
    if (ISwapchain* swapchain = hostSwapchain(app)) {
        const Extent2D extent = swapchain->getExtent();
        meta.swapchainWidth   = extent.width;
        meta.swapchainHeight  = extent.height;
        meta.presentMode      = toBenchmarkPresentModeLabel(swapchain->getPresentMode());
    }
    for (const EPresentMode::T mode : _availablePresentModes) {
        meta.availablePresentModes.emplace_back(toBenchmarkPresentModeLabel(mode));
    }

    const nlohmann::json summary = profiling::buildBenchSummaryJson(meta, aggregator);
    if (profiling::writeBenchSummaryFile(_summaryPath, summary)) {
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
        _bSummaryWriteFailed = true;
        YA_CORE_ERROR("Benchmark '{}': failed to write {}", _config.name, _summaryPath);
    }

    _frameRegistration.reset();
    _stageRegistration.reset();
    _passRegistration.reset();
    _counterRegistration.reset();
    _processMemoryRegistration.reset();
    profiling::setPassGpuTimingEnabled(_bSavedPassGpuTiming);
    _renderWeak = nullptr;
    app.requestQuit();
}

} // namespace ya
