#include "GUI/Host/GuiBenchmark.h"

#include "GUI/Host/GUIAppHost.h"

#include "Core/Log.h"
#include "Core/Profiling/BenchmarkConfig.h"
#include "RHI/Core/RenderSurfaceContext.h"
#include "RHI/Core/Swapchain.h"
#include "RHI/NativeWindow.h"
#include "RHI/PresentMode.h"
#include "RHI/Render.h"

#include <filesystem>
#include <format>

namespace ya
{

bool parseGuiBenchmarkJson(const nlohmann::json& json, FGuiBenchmarkConfig& config, std::string& error)
{
    profiling::FBenchmarkCommonConfig common;
    common.sourcePath   = config.sourcePath;
    common.name         = config.name;
    common.width        = config.width;
    common.height       = config.height;
    common.presentMode  = toPresentModeString(config.presentMode);
    common.warmupFrames = config.warmupFrames;
    common.sampleFrames = config.sampleFrames;
    common.outputDir    = config.outputDir;
    if (!profiling::parseBenchmarkCommonConfig(json, {"page"}, common, error)) {
        return false;
    }

    std::string page = config.page;
    if (!profiling::readBenchmarkString(json, "page", page, error)) {
        return false;
    }
    if (!tryParsePresentMode(common.presentMode, config.presentMode)) {
        error = std::format("'presentMode' must be immediate, mailbox, fifo or fifo_relaxed (got '{}')", common.presentMode);
        return false;
    }
    config.sourcePath   = std::move(common.sourcePath);
    config.name         = std::move(common.name);
    config.page         = std::move(page);
    config.width        = common.width;
    config.height       = common.height;
    config.warmupFrames = common.warmupFrames;
    config.sampleFrames = common.sampleFrames;
    config.outputDir    = std::move(common.outputDir);
    return true;
}

bool loadGuiBenchmarkConfig(const std::string& path, FGuiBenchmarkConfig& config, std::string& error)
{
    nlohmann::json json;
    if (!profiling::readBenchmarkJsonFile(path, json, error)) {
        return false;
    }
    config.sourcePath = path;
    if (config.name.empty()) {
        config.name = std::filesystem::path(path).stem().string();
    }
    if (!parseGuiBenchmarkJson(json, config, error)) {
        error = std::format("{}: {}", path, error);
        return false;
    }
    if (config.name.empty()) {
        config.name = std::filesystem::path(path).stem().string();
    }
    return true;
}

GuiBenchmarkRun::GuiBenchmarkRun(FGuiBenchmarkConfig config)
    : _config(std::move(config))
{
}

bool GuiBenchmarkRun::begin(GUIWindowHost& window)
{
    _window = &window;
    profiling::FBenchmarkCommonConfig common{
        .sourcePath   = _config.sourcePath,
        .name         = _config.name,
        .width        = _config.width,
        .height       = _config.height,
        .presentMode  = toPresentModeString(_config.presentMode),
        .warmupFrames = _config.warmupFrames,
        .sampleFrames = _config.sampleFrames,
        .outputDir    = _config.outputDir,
    };
    const profiling::FBenchmarkRunFacts facts{
        .project = "GUIWorkbench",
        .scene   = _config.page,
    };
    return _session.begin(*this, common, facts);
}

void GuiBenchmarkRun::onFrameEnd(GUIWindowHost& window)
{
    (void)window;
    _session.onFrameEnd();
}

profiling::FBenchmarkPresentModeSelection GuiBenchmarkRun::selectPresentMode(std::string_view requestedLabel)
{
    profiling::FBenchmarkPresentModeSelection result;
    IRenderSurfaceContext* surface   = _window ? _window->surfaceContext() : nullptr;
    ISwapchain*            swapchain = surface ? surface->getSwapchain() : nullptr;
    if (!swapchain) {
        YA_CORE_ERROR("GUI benchmark '{}': no host swapchain", _config.name);
        result.bOk = false;
        return result;
    }

    EPresentMode::T requested = EPresentMode::Immediate;
    if (!tryParsePresentMode(requestedLabel, requested)) {
        requested = EPresentMode::Immediate;
    }
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

void GuiBenchmarkRun::matchResolution(uint32_t width, uint32_t height)
{
    if (!_window) {
        return;
    }
    if (INativeWindow* native = _window->getNativeWindow()) {
        (void)native->matchDrawablePixelSize(width, height);
    }
}

void GuiBenchmarkRun::fillDeviceMeta(profiling::FBenchmarkDeviceMeta& meta)
{
    if (!_window) {
        return;
    }
    if (IRender* render = _window->getRender()) {
        const RenderDeviceInfo device = render->getDeviceInfo();
        meta.graphicsApi              = device.apiVersion;
        meta.gpuName                  = device.name;
        meta.gpuDriver                = device.driver;
    }
    if (IRenderSurfaceContext* surface = _window->surfaceContext()) {
        if (ISwapchain* swapchain = surface->getSwapchain()) {
            const Extent2D extent = swapchain->getExtent();
            meta.swapchainWidth   = extent.width;
            meta.swapchainHeight  = extent.height;
            meta.presentMode      = toPresentModeString(swapchain->getPresentMode());
        }
    }
}

void GuiBenchmarkRun::completedPassGpuTimes(std::unordered_map<std::string, float>& outPassTimesMs)
{
    if (_window) {
        if (IRender* render = _window->getRender()) {
            render->getCompletedPassGpuTimesMs(outPassTimesMs);
        }
    }
}

void GuiBenchmarkRun::requestQuit()
{
    if (_window) {
        _window->requestQuit();
    }
}

} // namespace ya
