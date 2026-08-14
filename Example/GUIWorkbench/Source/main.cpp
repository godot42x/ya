// GUIWorkbench: the real tool GUI app (gui-app-bootstrap Phase 3). A
// standalone binary consuming the shared ya-gui-host library; the tool
// workspace / shell / commands live in GUIWorkbench.* and never copy the
// SDL/Vulkan frame loop. No Scene/ECS/Render3D/Host/Editor dependency.

#include "Core/Log.h"

#include "GUI/Host/GUIApp.h"
#include "GUI/Host/GUIHeadlessHost.h"
#include "GUI/Draw2D/Render2D.h"
#include "GUI/Resources/FontManager.h"
#include "GUI/Widgets/UIFrameSnapshotDump.h"

#include "GUIWorkbench.h"

#include <cxxopts.hpp>

#include <exception>
#include <fstream>
#include <string>

namespace
{

void registerHeadlessWorkbenchFonts(const std::vector<uint32_t>& fontSizes)
{
    for (const uint32_t fontSize : fontSizes) {
        auto font        = std::make_shared<ya::Font>();
        font->fontSize   = static_cast<float>(fontSize);
        font->lineHeight = static_cast<float>(fontSize) * 1.3125f;
        font->ascent     = static_cast<float>(fontSize);
        for (uint32_t codePoint = 0; codePoint < 128; ++codePoint) {
            ya::Character character;
            character.advance  = {static_cast<float>(fontSize) * 0.6041667f, 0.0f};
            character.bInAtlas = true;
            font->characters[codePoint] = character;
        }
        ya::FontManager::get()->registerFont(ya::DEFAULT_RUNTIME_FONT_NAME, fontSize, std::move(font));
    }
}

bool writeHeadlessSnapshotJson(const ya::UIFrameSnapshot& snapshot, const std::string& path)
{
    std::ofstream output(path);
    if (!output) {
        YA_CORE_ERROR("GUIWorkbench headless: cannot write snapshot JSON '{}'", path);
        return false;
    }

    auto dump = ya::dumpUIFrameSnapshot(snapshot);
    dump["structuralDigest"] = ya::digestUIFrameSnapshot(snapshot);
    dump["semanticDigest"]   = ya::semanticDigestUIFrameSnapshot(snapshot);
    output << dump.dump(2);
    YA_CORE_INFO("GUIWorkbench headless wrote snapshot JSON to '{}' (structuralDigest={} semanticDigest={})",
                 path,
                 dump["structuralDigest"].get<uint64_t>(),
                 dump["semanticDigest"].get<uint64_t>());
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    ya::FGUIWindowHostConfig config;
    config.title     = "YA GUI Workbench";
    config.width     = 1280;
    config.height    = 800;
    config.fontSizes = {12, 13, 14, 15, 16};
    // Escape belongs to focused menus/modals/text interactions in the feature
    // gallery. Window close remains the standalone host exit path.
    config.bEscapeQuits = false;

    guiworkbench::FWorkbenchApp app;

    // One cxxopts parser for the whole GUI app entry (shared with the
    // engine's CliParams; no hand-written argv scanning).
    cxxopts::Options options(argv[0], "YA GUI Workbench");
    options.allow_unrecognised_options();
    options.add_options()
        ("exit-after-frame", "Quit gracefully after rendering N frames", cxxopts::value<uint64_t>()->default_value("0"))
        ("automation-control-port", "Automation control TCP port; 0 disables the server", cxxopts::value<uint16_t>()->default_value("0"))
        ("smoke-actions", "Run the end-to-end automation smoke")
        ("headless", "Build snapshots through GUIHeadlessHost instead of creating an SDL/Vulkan window")
        ("dump-snapshot", "Dump the frame snapshot BMP to path", cxxopts::value<std::string>())
        ("dump-snapshot-json", "Dump the structural frame snapshot JSON to path", cxxopts::value<std::string>())
        ("dump-frame", "Frame index at which to dump the snapshot", cxxopts::value<uint32_t>())
        ("gpu-shot", "Dump a GPU frame to path", cxxopts::value<std::string>())
        ("gpu-shot-frame", "Frame index for the GPU shot", cxxopts::value<uint32_t>())
        ("offscreen-shot", "Capture the same UI frame from a Framework-owned offscreen surface", cxxopts::value<std::string>())
        ("offscreen-shot-frame", "Frame index for the offscreen capture", cxxopts::value<uint32_t>())
        ("offscreen-diff", "Write zero-tolerance diff between --gpu-shot and --offscreen-shot", cxxopts::value<std::string>())
        ("scenario", "Run a JSONL input scenario instead of the SDL pump", cxxopts::value<std::string>())
        ("scenario-dump-dir", "Write per-checkpoint tree dumps into this dir", cxxopts::value<std::string>())
        ("scenario-capture", "Capture the final frame swapchain to this BMP", cxxopts::value<std::string>())
        ("scenario-golden", "Baseline BMP to diff the scenario capture against", cxxopts::value<std::string>())
        ("scenario-diff", "Write the scenario difference image to this path", cxxopts::value<std::string>())
        ("start-page", "Start the workbench on a named page (Render/Widgets/Layout/Menus/DragDrop/Modal/ScrollSplit/Editor)", cxxopts::value<std::string>())
        ("debug-render-overlay", "Inject a host-side render debug overlay into the UI snapshot")
        ("debug-render2d-log", "Enable Render2D session/clip/flush diagnostics in the log")
        ("debug-render2d-log-limit", "Maximum Render2D clip/flush logs per frame", cxxopts::value<uint32_t>()->default_value("16"));

    try {
        const auto result = options.parse(argc, argv);
        config.automation.exitAfterFrame = result["exit-after-frame"].as<uint64_t>();
        config.automation.controlPort    = result["automation-control-port"].as<uint16_t>();
        app.bSmokeActions = result.count("smoke-actions") > 0;
        if (result.count("dump-snapshot") > 0) {
            config.dumpSnapshotPath = result["dump-snapshot"].as<std::string>();
        }
        if (result.count("dump-snapshot-json") > 0) {
            config.dumpSnapshotJsonPath = result["dump-snapshot-json"].as<std::string>();
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
        if (result.count("offscreen-shot") > 0) {
            config.offscreenShotPath = result["offscreen-shot"].as<std::string>();
        }
        if (result.count("offscreen-shot-frame") > 0) {
            config.offscreenShotFrame = result["offscreen-shot-frame"].as<uint32_t>();
        }
        if (result.count("offscreen-diff") > 0) {
            config.offscreenDiffPath = result["offscreen-diff"].as<std::string>();
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
        if (result.count("start-page") > 0) {
            app.startPageName = result["start-page"].as<std::string>();
        }
        config.bDebugRenderOverlay = result.count("debug-render-overlay") > 0;
        if (result.count("debug-render2d-log") > 0) {
            auto& debug = ya::Render2D::debugState();
            debug.bLogSessionLifecycle = true;
            debug.bLogClipStack        = true;
            debug.bLogFlushBatches     = true;
            const uint32_t limit = result["debug-render2d-log-limit"].as<uint32_t>();
            debug.maxClipLogsPerFrame  = limit;
            debug.maxFlushLogsPerFrame = limit;
        }

        if (result.count("headless") > 0) {
            if (config.dumpSnapshotJsonPath.empty()) {
                YA_CORE_WARN("GUIWorkbench headless: --dump-snapshot-json is recommended for cross-path evidence");
            }
            registerHeadlessWorkbenchFonts(config.fontSizes);
            ya::GUIHeadlessHost host(
                ya::FGUIHeadlessHostConfig{
                    .logicalExtent = {config.width, config.height},
                    .automation    = config.automation,
                },
                app);
            if (!host.init()) {
                return 1;
            }
            const int runResult = host.run();
            if (!config.dumpSnapshotJsonPath.empty() &&
                !writeHeadlessSnapshotJson(host.getLastSnapshot(), config.dumpSnapshotJsonPath)) {
                return 1;
            }
            host.shutdown();
            if (app.bSmokeActions) {
                YA_CORE_INFO("GUIWorkbench smoke result: {}", app.getSmokePassed() ? "PASS" : "FAIL");
                return app.getSmokePassed() ? 0 : 1;
            }
            return runResult;
        }
    }
    catch (const std::exception& e) {
        YA_CORE_WARN("GUIWorkbench: failed to parse command line: {}", e.what());
    }

    ya::GUIApp guiApp(config, app);
    if (!guiApp.init()) {
        return 1;
    }
    const int result = guiApp.run();
    guiApp.shutdown();
    if (app.bSmokeActions) {
        YA_CORE_INFO("GUIWorkbench smoke result: {}", app.getSmokePassed() ? "PASS" : "FAIL");
        return app.getSmokePassed() ? 0 : 1;
    }
    YA_CORE_INFO("GUIWorkbench finished");
    return result;
}
