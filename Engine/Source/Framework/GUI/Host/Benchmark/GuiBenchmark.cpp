#include "GUI/Host/GuiBenchmark.h"

#include "GUI/Host/GUIAppHost.h"

#include "Core/Log.h"
#include "Core/Profiling/BenchmarkConfig.h"
#include "RHI/Core/RenderSurfaceContext.h"
#include "RHI/Core/Swapchain.h"
#include "RHI/PresentMode.h"
#include "RHI/Render.h"

#include <filesystem>
#include <format>

namespace ya
{

bool parseGuiBenchmarkJson(const nlohmann::json& json, FGuiBenchmarkConfig& config, std::string& error)
{
    if (!profiling::parseBenchmarkCommonConfig(json, {"page"}, config.common, error)) {
        return false;
    }
    if (!profiling::isBenchmarkPresentModeLabel(config.common.presentMode)) {
        error = std::format("'presentMode' must be immediate, mailbox, fifo or fifo_relaxed (got '{}')", config.common.presentMode);
        return false;
    }
    return profiling::readBenchmarkString(json, "page", config.page, error);
}

bool loadGuiBenchmarkConfig(const std::string& path, FGuiBenchmarkConfig& config, std::string& error)
{
    nlohmann::json json;
    if (!profiling::readBenchmarkJsonFile(path, json, error)) {
        return false;
    }
    config.common.sourcePath = path;
    if (!parseGuiBenchmarkJson(json, config, error)) {
        error = std::format("{}: {}", path, error);
        return false;
    }
    if (config.common.name.empty()) {
        config.common.name = std::filesystem::path(path).stem().string();
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
    const profiling::FBenchmarkRunFacts facts{
        .project = "GUIWorkbench",
        .scene   = _config.page,
    };
    return _session.begin(*this, _config.common, facts);
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
        YA_CORE_ERROR("GUI benchmark '{}': no host swapchain", _config.common.name);
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
    (void)_window->matchDrawablePixelSize(width, height);
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
