// GUIWorkbench: the real tool GUI app (gui-app-bootstrap Phase 3). A
// standalone binary consuming the shared ya-gui-app-host library; the tool
// workspace / shell / commands live in GUIWorkbench.* and never copy the
// SDL/Vulkan frame loop. No Scene/ECS/Render3D/Host/Editor dependency.

#include "Core/Log.h"

#include "GUI/App/GUIAppHost.h"

#include "GUIWorkbench.h"

#include <cxxopts.hpp>

#include <exception>
#include <string>

int main(int argc, char** argv)
{
    ya::FGUIAppHostConfig config;
    config.title     = "YA GUI Workbench";
    config.width     = 1280;
    config.height    = 800;
    config.fontSizes = {12, 13, 14, 15, 16};

    guiworkbench::FWorkbenchApp app;

    // One cxxopts parser for the whole GUI app entry (shared with the
    // engine's CliParams; no hand-written argv scanning).
    cxxopts::Options options(argv[0], "YA GUI Workbench");
    options.allow_unrecognised_options();
    options.add_options()
        ("exit-after-frame", "Quit gracefully after rendering N frames", cxxopts::value<uint64_t>()->default_value("0"))
        ("automation-control-port", "Automation control TCP port; 0 disables the server", cxxopts::value<uint16_t>()->default_value("0"))
        ("smoke-actions", "Run the end-to-end automation smoke")
        ("dump-snapshot", "Dump the frame snapshot JSON to path", cxxopts::value<std::string>())
        ("dump-frame", "Frame index at which to dump the snapshot", cxxopts::value<uint32_t>())
        ("gpu-shot", "Dump a GPU frame to path", cxxopts::value<std::string>())
        ("gpu-shot-frame", "Frame index for the GPU shot", cxxopts::value<uint32_t>())
        ("scenario", "Run a JSONL input scenario instead of the SDL pump", cxxopts::value<std::string>())
        ("scenario-dump-dir", "Write per-checkpoint tree dumps into this dir", cxxopts::value<std::string>())
        ("scenario-capture", "Capture the final frame swapchain to this BMP", cxxopts::value<std::string>())
        ("scenario-golden", "Baseline BMP to diff the scenario capture against", cxxopts::value<std::string>())
        ("scenario-diff", "Write the scenario difference image to this path", cxxopts::value<std::string>());

    try {
        const auto result = options.parse(argc, argv);
        config.automation.exitAfterFrame = result["exit-after-frame"].as<uint64_t>();
        config.automation.controlPort    = result["automation-control-port"].as<uint16_t>();
        app.bSmokeActions = result.count("smoke-actions") > 0;
        if (result.count("dump-snapshot") > 0) {
            config.dumpSnapshotPath = result["dump-snapshot"].as<std::string>();
        }
        if (result.count("dump-frame") > 0) {
            config.dumpFrame = result["dump-frame"].as<uint32_t>();
        }
        if (result.count("gpu-shot") > 0) {
            config.gpuShotPath = result["gpu-shot"].as<std::string>();
        }
        if (result.count("gpu-shot-frame") > 0) {
            config.gpuShotFrame = result["gpu-shot-frame"].as<uint32_t>();
        }
        if (result.count("scenario") > 0) {
            config.scenarioPath = result["scenario"].as<std::string>();
        }
        if (result.count("scenario-dump-dir") > 0) {
            config.scenarioDumpDir = result["scenario-dump-dir"].as<std::string>();
        }
        if (result.count("scenario-capture") > 0) {
            config.scenarioCapturePath = result["scenario-capture"].as<std::string>();
        }
        if (result.count("scenario-golden") > 0) {
            config.scenarioGoldenPath = result["scenario-golden"].as<std::string>();
        }
        if (result.count("scenario-diff") > 0) {
            config.scenarioDiffPath = result["scenario-diff"].as<std::string>();
        }
    }
    catch (const std::exception& e) {
        YA_CORE_WARN("GUIWorkbench: failed to parse command line: {}", e.what());
    }

    ya::GUIAppHost host(config, app);
    if (!host.init()) {
        return 1;
    }
    const int result = host.run();
    host.shutdown();
    if (app.bSmokeActions) {
        YA_CORE_INFO("GUIWorkbench smoke result: {}", app.getSmokePassed() ? "PASS" : "FAIL");
        return app.getSmokePassed() ? 0 : 1;
    }
    YA_CORE_INFO("GUIWorkbench finished");
    return result;
}
