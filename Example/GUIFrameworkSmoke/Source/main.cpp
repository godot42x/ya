// GUIFrameworkSmoke: the minimal end-to-end standalone GUI smoke, now an
// executable consumer of the ya-gui-app-host library (see ../xmake.lua).
//
// The smoke only supplies host configuration, the demo content and the
// frame-count CLI; the window / RHI / input pump / snapshot / compose /
// present lifecycle is owned by GUIApp + GUIWindowHost. The binary exercises the GUI
// product closure only (Core/RHI/Vulkan backend + the four GUI modules +
// the app host); no ECS/Physics/Resource/RenderGraph/Render3D/Host/Editor.

#include "GUI/App/GUIApp.h"

#include "Core/Application/AutomationRun.h"
#include "Core/Log.h"

#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/WidgetTree.h"

#include <format>
#include <memory>

using namespace ya;

namespace
{

/// Interactive demo content: a panel with a title, a click counter label and
/// a button. The button label is a Pass-filtered child text so hover/press
/// still reach the button underneath.
struct FMinimalUIDemo
{
    std::shared_ptr<UIPanel>  panel;
    std::shared_ptr<UIText>   title;
    std::shared_ptr<UIText>   counter;
    std::shared_ptr<UIButton> button;
    std::shared_ptr<UIText>   buttonLabel;
};

void buildDemoContent(WidgetTree& tree, FMinimalUIDemo& demo)
{
    demo.panel = std::make_shared<UIPanel>("DemoPanel");
    demo.panel->_position = {64.0f, 64.0f};
    demo.panel->_size     = {340.0f, 200.0f};
    demo.panel->_color    = {0.13f, 0.14f, 0.17f, 0.96f};

    demo.title = std::make_shared<UIText>("Title");
    demo.title->_position = {16.0f, 14.0f};
    demo.title->_size     = {308.0f, 30.0f};
    demo.title->_fontSize = 20;
    demo.title->_text     = "YA Minimal GUI Host";
    demo.title->_color    = {1.0f, 1.0f, 1.0f, 1.0f};

    demo.counter = std::make_shared<UIText>("Counter");
    demo.counter->_position = {16.0f, 58.0f};
    demo.counter->_size     = {308.0f, 26.0f};
    demo.counter->_fontSize = 16;
    demo.counter->_text     = "Clicked: 0";
    demo.counter->_color    = {0.85f, 0.87f, 0.90f, 1.0f};

    demo.button = std::make_shared<UIButton>("ClickButton");
    demo.button->_position = {16.0f, 100.0f};
    demo.button->_size     = {150.0f, 44.0f};
    demo.button->_normalColor  = {0.22f, 0.48f, 0.86f, 1.0f};
    demo.button->_hoveredColor = {0.32f, 0.58f, 0.96f, 1.0f};
    demo.button->_pressedColor = {0.14f, 0.34f, 0.66f, 1.0f};

    demo.buttonLabel = std::make_shared<UIText>("ButtonLabel");
    demo.buttonLabel->_size     = {150.0f, 44.0f};
    demo.buttonLabel->_fontSize = 16;
    demo.buttonLabel->_text     = "Click me";
    demo.buttonLabel->_color    = {1.0f, 1.0f, 1.0f, 1.0f};
    demo.buttonLabel->_hAlign   = EWidgetAlignH::Center;
    demo.buttonLabel->_vAlign   = EWidgetAlignV::Center;

    // Shared state captured by value: the lambda stays valid even if the demo
    // struct goes out of scope (the tree owns the widgets either way).
    auto clickCount = std::make_shared<uint32_t>(0);
    demo.button->_onClick = [counter = demo.counter, clickCount]() {
        ++(*clickCount);
        counter->_text = std::format("Clicked: {}", *clickCount);
        YA_CORE_INFO("Minimal host button clicked (count {})", *clickCount);
    };

    tree.attachToLayer(WidgetTree::ELayer::Content, demo.panel);
    tree.attach(*demo.panel, demo.title);
    tree.attach(*demo.panel, demo.counter);
    tree.attach(*demo.panel, demo.button);
    tree.attach(*demo.button, demo.buttonLabel);
}

struct FSmokeApp final : IGUIAppDelegate
{
    void buildUI(WidgetTree& tree) override { buildDemoContent(tree, demo); }

    FMinimalUIDemo demo;
};

} // namespace

int main(int argc, char** argv)
{
    FGUIWindowHostConfig config;
    config.title      = "YA Minimal GUI Host";
    config.width      = 1024;
    config.height     = 768;
    config.bResizable = true;
    applyAutomationRunArgs(argc, argv, config.automation);

    FSmokeApp app;
    GUIApp guiApp(config, app);
    if (!guiApp.init()) {
        return 1;
    }
    const int result = guiApp.run();
    guiApp.shutdown();
    if (config.automation.exitAfterFrame > 0) {
        YA_CORE_INFO("Minimal GUI host finished after automation frame budget {}",
                     config.automation.exitAfterFrame);
    }
    else {
        YA_CORE_INFO("Minimal GUI host finished");
    }
    return result;
}
