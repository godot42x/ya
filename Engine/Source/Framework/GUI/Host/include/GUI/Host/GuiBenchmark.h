#pragma once

// Windowed GUI benchmark. Same `bench_summary.json` the game runtime writes.
// The warmup / sample / summary state machine is `profiling::BenchmarkSession`.
// This adapter only selects the gallery page.

#include "Core/Api.h"
#include "Core/Profiling/BenchmarkConfig.h"
#include "Core/Profiling/BenchmarkSession.h"

#include <nlohmann/json.hpp>

#include <string>

namespace ya
{

class GUIWindowHost;

struct FGuiBenchmarkConfig
{
    profiling::FBenchmarkCommonConfig common;
    /// Gallery page to open when the CLI did not pass `--start-page`.
    std::string page;
};

[[nodiscard]] YA_GUI_API bool parseGuiBenchmarkJson(const nlohmann::json& json, FGuiBenchmarkConfig& config, std::string& error);
[[nodiscard]] YA_GUI_API bool loadGuiBenchmarkConfig(const std::string& path, FGuiBenchmarkConfig& config, std::string& error);

/// One windowed run. `begin` after the window exists; `onFrameEnd` once per
/// presented tick, after the host tick's perf scope has closed.
class YA_GUI_API GuiBenchmarkRun : private profiling::IBenchmarkHost
{
    FGuiBenchmarkConfig             _config;
    profiling::BenchmarkSession     _session;
    GUIWindowHost*                  _window = nullptr;

  public:
    explicit GuiBenchmarkRun(FGuiBenchmarkConfig config);

    [[nodiscard]] const FGuiBenchmarkConfig& config() const { return _config; }
    [[nodiscard]] bool                       failed() const { return _session.failed(); }

    [[nodiscard]] bool begin(GUIWindowHost& window);
    void               onFrameEnd(GUIWindowHost& window);

  private:
    [[nodiscard]] profiling::FBenchmarkPresentModeSelection selectPresentMode(std::string_view requestedLabel) override;
    void                                                    matchResolution(uint32_t width, uint32_t height) override;
    void                                                    fillDeviceMeta(profiling::FBenchmarkDeviceMeta& meta) override;
    void completedPassGpuTimes(std::unordered_map<std::string, float>& outPassTimesMs) override;
    void                                                    requestQuit() override;
};

} // namespace ya
