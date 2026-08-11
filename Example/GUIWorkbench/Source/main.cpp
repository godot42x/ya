// GUIWorkbench: the real tool GUI app (gui-app-bootstrap Phase 3). A
// standalone binary consuming the shared ya-gui-app-host library; the tool
// workspace / shell / commands live in GUIWorkbench.* and never copy the
// SDL/Vulkan frame loop. No Scene/ECS/Render3D/Host/Editor dependency.

#include "Core/Log.h"

#include "AppServices/AppAutomationRun.h"
#include "GUI/App/GUIAppHost.h"

#include "GUIWorkbench.h"

#include <string>

int main(int argc, char** argv)
{
    ya::FGUIAppHostConfig config;
    config.title     = "YA GUI Workbench";
    config.width     = 1280;
    config.height    = 800;
    config.fontSizes = {13, 14, 16};
    ya::applyAutomationRunArgs(argc, argv, config.automation);

    guiworkbench::FWorkbenchApp app;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--smoke-actions") {
            app.bSmokeActions = true;
        }
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
