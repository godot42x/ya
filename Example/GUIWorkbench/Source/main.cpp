// GUIWorkbench: the real tool GUI app (gui-app-bootstrap Phase 3). A
// standalone binary consuming the shared ya-gui-app-host library; the tool
// workspace / shell / commands live in GUIWorkbench.* and never copy the
// SDL/Vulkan frame loop. No Scene/ECS/Render3D/Host/Editor dependency.

#include "Core/Log.h"

#include "GUI/App/GUIAppHost.h"

#include "GUIWorkbench.h"

#include <cstdlib>
#include <string>

namespace
{

uint64_t parseExitAfterFrame(int argc, char** argv)
{
    uint64_t exitAfterFrame = 120;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--exit-after-frame") {
            if (i + 1 < argc) {
                exitAfterFrame = static_cast<uint64_t>(std::strtoull(argv[++i], nullptr, 10));
            }
        }
        else if (arg.starts_with("--exit-after-frame=")) {
            exitAfterFrame = static_cast<uint64_t>(
                std::strtoull(arg.c_str() + std::string("--exit-after-frame=").size(), nullptr, 10));
        }
    }
    return exitAfterFrame;
}

} // namespace

int main(int argc, char** argv)
{
    const uint64_t exitAfterFrame = parseExitAfterFrame(argc, argv);

    ya::FGUIAppHostConfig config;
    config.title     = "YA GUI Workbench";
    config.width     = 1280;
    config.height    = 800;
    config.fontSizes = {13, 14, 16};

    guiworkbench::FWorkbenchApp app;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--smoke-actions") {
            app.bSmokeActions = true;
        }
        else if (arg.starts_with("--yaui=")) {
            app.startupDocumentPath = arg.substr(std::string("--yaui=").size());
        }
    }
    ya::GUIAppHost host(config, app);
    if (!host.init()) {
        return 1;
    }
    const int result = host.run(exitAfterFrame);
    host.shutdown();
    if (app.bSmokeActions) {
        YA_CORE_INFO("GUIWorkbench smoke result: {}", app.getSmokePassed() ? "PASS" : "FAIL");
        return app.getSmokePassed() ? 0 : 1;
    }
    YA_CORE_INFO("GUIWorkbench finished");
    return result;
}
