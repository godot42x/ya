// GUIWorkbench: the real tool GUI app (gui-app-bootstrap). Phase 1 ships an
// empty shell — title + panel — proving that a second consumer can mount its
// own content on the shared ya-gui-app-host without copying any host code.
// Phase 3 replaces the shell with the document tree / preview / inspector /
// command workspace.

#include "Core/Log.h"

#include "GUI/App/GUIAppHost.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/WidgetTree.h"

#include <memory>
#include <cstdlib>
#include <string>

using namespace ya;

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

struct FWorkbenchShell final : IGUIAppDelegate
{
    void buildUI(WidgetTree& tree) override
    {
        panel = std::make_shared<UIPanel>("WorkbenchRoot");
        panel->_position = {0.0f, 0.0f};
        panel->_size     = {1024.0f, 768.0f};
        panel->_color    = {0.10f, 0.11f, 0.13f, 1.0f};

        title = std::make_shared<UIText>("Title");
        title->_position = {24.0f, 20.0f};
        title->_size     = {500.0f, 32.0f};
        title->_fontSize = 22;
        title->_text     = "GUI Workbench";
        title->_color    = {1.0f, 1.0f, 1.0f, 1.0f};

        subtitle = std::make_shared<UIText>("Subtitle");
        subtitle->_position = {24.0f, 60.0f};
        subtitle->_size     = {700.0f, 26.0f};
        subtitle->_fontSize = 16;
        subtitle->_text     = "Shell only - tool workspace arrives in Phase 3";
        subtitle->_color    = {0.62f, 0.66f, 0.72f, 1.0f};

        tree.attachToLayer(WidgetTree::ELayer::Content, panel);
        tree.attach(*panel, title);
        tree.attach(*panel, subtitle);
    }

    std::shared_ptr<UIPanel> panel;
    std::shared_ptr<UIText>  title;
    std::shared_ptr<UIText>  subtitle;
};

} // namespace

int main(int argc, char** argv)
{
    const uint64_t exitAfterFrame = parseExitAfterFrame(argc, argv);

    FGUIAppHostConfig config;
    config.title  = "YA GUI Workbench";
    config.width  = 1280;
    config.height = 800;

    FWorkbenchShell app;
    GUIAppHost host(config, app);
    if (!host.init()) {
        return 1;
    }
    const int result = host.run(exitAfterFrame);
    host.shutdown();
    YA_CORE_INFO("GUIWorkbench finished");
    return result;
}
