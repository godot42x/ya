#include "GameRuntime/Benchmark/BenchmarkRun.h"

#include "Core/Log.h"
#include "ECS/Entity.h"
#include "GameRuntime/App.h"
#include "GameRuntime/Lifecycle/FPSCtrl.h"
#include "GameRuntime/Render/SceneCameraQuery.h"
#include "RHI/Core/RenderSurfaceContext.h"
#include "RHI/Core/Swapchain.h"
#include "RHI/NativeWindow.h"
#include "RHI/PresentMode.h"
#include "RHI/Render.h"
#include "Scene3D/TransformComponent.h"

namespace ya
{

namespace
{

ISwapchain* hostSwapchain(App& app)
{
    IRenderSurfaceContext* surface = app.getRenderServices().getHostSurface();
    return surface ? surface->getSwapchain() : nullptr;
}

} // namespace

BenchmarkRun::BenchmarkRun(BenchmarkConfig config)
    : _config(std::move(config))
{
}

profiling::FBenchmarkRunFacts BenchmarkRun::currentFacts() const
{
    profiling::FBenchmarkRunFacts facts = _facts;
    if (_app) {
        facts.bFrameRateCapped = FPSControl::get()->bEnable;
        facts.bCpuTrace        = _app->getDesc().profiling.bCpuProfileEnabled;
    }
    return facts;
}

void BenchmarkRun::begin(App& app, FBenchmarkRunContext context)
{
    _app     = &app;
    _context = std::move(context);
    if (_config.bUncapFrameRate) {
        FPSControl::get()->bEnable = false;
    }
    app.getRenderServices().setRenderResolution(Extent2D{.width = _config.common.width, .height = _config.common.height});

    _facts = profiling::FBenchmarkRunFacts{
        .project            = _context.project,
        .scene              = _context.scene,
        .bFrameRateCapped   = FPSControl::get()->bEnable,
        .fixedDeltaSeconds  = _config.fixedDeltaSeconds,
        .bCpuTrace          = app.getDesc().profiling.bCpuProfileEnabled,
        .cameraKeyframes    = _config.cameraPath.keyframes.size(),
    };

    (void)_session.begin(*this, _config.common, _facts);
}

float BenchmarkRun::frameDelta(float wallDeltaSeconds) const
{
    return _config.fixedDeltaSeconds > 0.0f ? _config.fixedDeltaSeconds : wallDeltaSeconds;
}

void BenchmarkRun::applyCameraPath(App& app) const
{
    const std::optional<FBenchmarkCameraPose> pose = sampleBenchmarkCameraPath(_config.cameraPath, _session.frameIndex());
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
    (void)app;
    _session.setFacts(currentFacts());
    _session.onFrameEnd();
}

profiling::FBenchmarkPresentModeSelection BenchmarkRun::selectPresentMode(std::string_view requestedLabel)
{
    profiling::FBenchmarkPresentModeSelection result;
    EPresentMode::T                           requested = EPresentMode::Immediate;
    if (!tryParsePresentMode(requestedLabel, requested)) {
        requested = EPresentMode::Immediate;
    }
    if (ISwapchain* swapchain = _app ? hostSwapchain(*_app) : nullptr) {
        const std::vector<EPresentMode::T> available = swapchain->getAvailablePresentModes();
        result.available.reserve(available.size());
        for (const EPresentMode::T mode : available) {
            result.available.emplace_back(toPresentModeString(mode));
        }
        const EPresentMode::T chosen = choosePreferredPresentMode(requested, available);
        if (chosen != swapchain->getPresentMode()) {
            swapchain->setPresentMode(chosen);
        }
        result.selected = toPresentModeString(chosen);
        return result;
    }
    YA_CORE_WARN("Benchmark: no host swapchain; present mode left as is");
    result.selected = "fifo";
    return result;
}

void BenchmarkRun::matchResolution(uint32_t width, uint32_t height)
{
    if (_context.mainWindow) {
        (void)_context.mainWindow->matchDrawablePixelSize(width, height);
    }
}

void BenchmarkRun::fillDeviceMeta(profiling::FBenchmarkDeviceMeta& meta)
{
    if (!_app) {
        return;
    }
    if (IRender* render = _app->getRenderServices().getRender()) {
        const RenderDeviceInfo device = render->getDeviceInfo();
        meta.graphicsApi              = device.apiVersion;
        meta.gpuName                  = device.name;
        meta.gpuDriver                = device.driver;
    }
    if (ISwapchain* swapchain = hostSwapchain(*_app)) {
        const Extent2D extent = swapchain->getExtent();
        meta.swapchainWidth   = extent.width;
        meta.swapchainHeight  = extent.height;
        meta.presentMode      = toPresentModeString(swapchain->getPresentMode());
    }
}

void BenchmarkRun::completedPassGpuTimes(std::unordered_map<std::string, float>& outPassTimesMs)
{
    if (_app) {
        if (IRender* render = _app->getRenderServices().getRender()) {
            render->getCompletedPassGpuTimesMs(outPassTimesMs);
        }
    }
}

void BenchmarkRun::requestQuit()
{
    if (_app) {
        _app->requestQuit();
    }
}

} // namespace ya
