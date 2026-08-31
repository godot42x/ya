// GUIFrameworkSmoke: the minimal end-to-end standalone GUI smoke, now an
// executable consumer of the ya-gui-host library (see ../xmake.lua).
//
// The smoke only supplies host configuration, the demo content and the
// frame-count CLI; the window / RHI / input pump / snapshot / compose /
// present lifecycle is owned by GUIApp + GUIWindowHost. The binary exercises the GUI
// product closure only (Core/RHI/Vulkan backend + the four GUI modules +
// the app host); no ECS/Physics/Resource/RenderGraph/Render3D/Host/Editor.

#include "GUI/Host/GUIApp.h"

#include "App/Control/AutomationRun.h"
#include "Core/Log.h"

#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Theme.h"
#include "GUI/Widgets/WidgetTree.h"

#include <format>
#include <memory>

using namespace ya;

namespace
{

void setPendingSlotPosition(UIElement& widget, glm::vec2 value)
{
    widget.setPendingSlotInitializer([value](UIElement&, UISlot& edge) {
        if (auto* canvas = edge.as<UICanvasSlot>()) {
            canvas->setOffset(value);
        }
    });
}

void setPendingSlotSize(UIElement& widget, glm::vec2 value)
{
    widget.setPendingSlotInitializer([value](UIElement&, UISlot& edge) {
        if (auto* canvas = edge.as<UICanvasSlot>()) {
            canvas->setFixedSize(value);
            canvas->setWidthSizeMode(EWidgetSizeMode::Fixed);
            canvas->setHeightSizeMode(EWidgetSizeMode::Fixed);
        }
        else if (auto* box = edge.as<UIBoxSlot>()) {
            box->setPreferredSize(value);
        }
        else if (auto* overlay = edge.as<UIOverlaySlot>()) {
            overlay->setPreferredSize(value);
        }
    });
}

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
    setPendingSlotPosition(*demo.panel, {64.0f, 64.0f});
    setPendingSlotSize(*demo.panel, {340.0f, 200.0f});
    demo.panel->setColor({0.13f, 0.14f, 0.17f, 0.96f});

    demo.title = std::make_shared<UIText>("Title");
    setPendingSlotPosition(*demo.title, {16.0f, 14.0f});
    setPendingSlotSize(*demo.title, {308.0f, 30.0f});
    demo.title->_fontSize = 20;
    demo.title->setText("YA Minimal GUI Host");
    demo.title->setColor({1.0f, 1.0f, 1.0f, 1.0f});

    demo.counter = std::make_shared<UIText>("Counter");
    setPendingSlotPosition(*demo.counter, {16.0f, 58.0f});
    setPendingSlotSize(*demo.counter, {308.0f, 26.0f});
    demo.counter->_fontSize = 16;
    demo.counter->setText("Clicked: 0");
    demo.counter->setColor({0.85f, 0.87f, 0.90f, 1.0f});

    demo.button = std::make_shared<UIButton>("ClickButton");
    setPendingSlotPosition(*demo.button, {16.0f, 100.0f});
    setPendingSlotSize(*demo.button, {150.0f, 44.0f});
    // Button fills come from the mounted theme ("button" key) — style-system
    // Phase 3 cleanup removed the bare color fields (see FSmokeApp::buildUI).

    demo.buttonLabel = std::make_shared<UIText>("ButtonLabel");
    setPendingSlotSize(*demo.buttonLabel, {150.0f, 44.0f});
    demo.buttonLabel->_fontSize = 16;
    demo.buttonLabel->setText("Click me");
    demo.buttonLabel->setColor({1.0f, 1.0f, 1.0f, 1.0f});
    demo.buttonLabel->_hAlign   = EWidgetAlignH::Center;
    demo.buttonLabel->_vAlign   = EWidgetAlignV::Center;

    // Shared state captured by value: the lambda stays valid even if the demo
    // struct goes out of scope (the tree owns the widgets either way).
    auto clickCount = std::make_shared<uint32_t>(0);
    demo.button->_onClick = [counter = demo.counter, clickCount]() {
        ++(*clickCount);
        counter->setText(std::format("Clicked: {}", *clickCount));
        YA_CORE_INFO("Minimal host button clicked (count {})", *clickCount);
    };

    FCanvasSlotArgs panelSlot;
    panelSlot.anchorMin = {0.0f, 0.0f};
    panelSlot.anchorMax = {0.0f, 0.0f};
    panelSlot.offset = {64.0f, 64.0f};
    panelSlot.fixedSize = {340.0f, 200.0f};
    tree.attachToLayer(WidgetTree::ELayer::Content, demo.panel, panelSlot);
    tree.attach(*demo.panel, demo.title);
    tree.attach(*demo.panel, demo.counter);
    tree.attach(*demo.panel, demo.button);
    tree.attach(*demo.button, demo.buttonLabel);
}

struct FSmokeApp final : IGUIAppDelegate
{
    void buildUI(WidgetTree& tree) override
    {
        // Theme CONTENT (style-system Phase 4): the smoke app owns a small
        // UITheme that gives its demo button the blue look; the framework
        // only resolves + invalidates. The theme must outlive buildUI
        // (WidgetTree stores a raw pointer).
        theme = std::make_shared<ya::UITheme>();
        auto style       = ya::FButtonStyle{};
        style.normalFill = ya::FBrush::solid({0.22f, 0.48f, 0.86f, 1.0f});
        style.hoveredFill = ya::FBrush::solid({0.32f, 0.58f, 0.96f, 1.0f});
        style.pressedFill = ya::FBrush::solid({0.14f, 0.34f, 0.66f, 1.0f});
        theme->define<ya::FButtonStyle>("button", style);
        tree.setTheme(theme.get());

        buildDemoContent(tree, demo);
    }

    FMinimalUIDemo demo;
    std::shared_ptr<ya::UITheme> theme;
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
