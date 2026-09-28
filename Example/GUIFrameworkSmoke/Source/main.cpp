// GUIFrameworkSmoke: the minimal end-to-end standalone GUI smoke, now an
// executable consumer of the ya-gui-host library (see ../xmake.lua).
//
// The smoke only supplies host configuration, the demo content and the
// frame-count CLI; the window / RHI / input pump / snapshot / compose /
// present lifecycle is owned by GUIApp + GUIWindowHost. The binary exercises the GUI
// product closure only (Core/RHI/Vulkan backend + the four GUI modules +
// the app host); no ECS/Physics/Resource/RenderGraph/Render3D/Host/Editor.

#include "GUI/Host/GUIAppHost.h"

#include "App/Control/AutomationRun.h"
#include "Core/Log.h"

#include "GUI/Widgets/Controls/Border.h"
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

void attachCanvasChild(WidgetTree& tree, UIElement& parent, const UIElementRef& child,
                       glm::vec2 position, glm::vec2 size)
{
    const WidgetAttachment attached = tree.attach(parent, child, [position, size](UIElement&, UISlot& edge) {
        if (auto* canvas = edge.as<UICanvasSlot>()) {
            canvas->setOffset(position);
            canvas->setFixedSize(size);
            canvas->setWidthSizeMode(EWidgetSizeMode::Fixed);
            canvas->setHeightSizeMode(EWidgetSizeMode::Fixed);
        }
    });
    YA_CORE_ASSERT(attached.valid(), "GUIFrameworkSmoke: failed to attach '{}'", child->_name);
}

/// Interactive demo content: a panel with a title, a click counter label and
/// a button. The button label is a Pass-filtered child text so hover/press
/// still reach the button underneath.
struct FMinimalUIDemo
{
    std::shared_ptr<UICanvasPanel>  panel;
    std::shared_ptr<UIBorder>       fill;
    std::shared_ptr<UIText>   title;
    std::shared_ptr<UIText>   counter;
    std::shared_ptr<UIButton> button;
    std::shared_ptr<UIText>   buttonLabel;
};

void buildDemoContent(WidgetTree& tree, FMinimalUIDemo& demo)
{
    demo.panel = std::make_shared<UICanvasPanel>("DemoPanel");
    demo.fill  = std::make_shared<UIBorder>("DemoPanelFill");
    demo.fill->setColor({0.13f, 0.14f, 0.17f, 0.96f});
    demo.fill->setVisibility(EWidgetVisibility::HitTestInvisible);

    demo.title = std::make_shared<UIText>("Title");
    demo.title->_fontSize = 20;
    demo.title->setText("YA Minimal GUI Host");
    demo.title->setColor({1.0f, 1.0f, 1.0f, 1.0f});

    demo.counter = std::make_shared<UIText>("Counter");
    demo.counter->_fontSize = 16;
    demo.counter->setText("Clicked: 0");
    demo.counter->setColor({0.85f, 0.87f, 0.90f, 1.0f});

    demo.button = std::make_shared<UIButton>("ClickButton");
    // Button fills come from the mounted theme ("button" key) — style-system
    // Phase 3 cleanup removed the bare color fields (see FSmokeApp::buildUI).

    demo.buttonLabel = std::make_shared<UIText>("ButtonLabel");
    demo.buttonLabel->_fontSize = 16;
    demo.buttonLabel->setText("Click me");
    demo.buttonLabel->setColor({1.0f, 1.0f, 1.0f, 1.0f});
    demo.buttonLabel->_hAlign   = EWidgetAlignH::Center;
    demo.buttonLabel->_vAlign   = EWidgetAlignV::Center;

    // Shared state captured by value: the lambda stays valid even if the demo
    // struct goes out of scope (the tree owns the widgets either way).
    auto clickCount = std::make_shared<uint32_t>(0);
    demo.button->onClicked.addLambda([counter = demo.counter, clickCount]() {
        ++(*clickCount);
        counter->setText(std::format("Clicked: {}", *clickCount));
        YA_CORE_INFO("Minimal host button clicked (count {})", *clickCount);
    });

    FCanvasSlotArgs panelSlot;
    panelSlot.anchorMin = {0.0f, 0.0f};
    panelSlot.anchorMax = {0.0f, 0.0f};
    panelSlot.offset = {64.0f, 64.0f};
    panelSlot.fixedSize = {340.0f, 200.0f};
    tree.attachToLayer(WidgetTree::ELayer::Content, demo.panel, panelSlot);
    FCanvasSlotArgs fillSlot;
    fillSlot.anchorMin = {0.0f, 0.0f};
    fillSlot.anchorMax = {1.0f, 1.0f};
    tree.attach(*demo.panel, demo.fill, fillSlot);
    attachCanvasChild(tree, *demo.panel, demo.title, {16.0f, 14.0f}, {308.0f, 30.0f});
    attachCanvasChild(tree, *demo.panel, demo.counter, {16.0f, 58.0f}, {308.0f, 26.0f});
    attachCanvasChild(tree, *demo.panel, demo.button, {16.0f, 100.0f}, {150.0f, 44.0f});
    attachCanvasChild(tree, *demo.button, demo.buttonLabel, {0.0f, 0.0f}, {150.0f, 44.0f});
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
    if (config.automation.exitAfterTick > 0) {
        YA_CORE_INFO("Minimal GUI host finished after automation frame budget {}",
                     config.automation.exitAfterTick);
    }
    else {
        YA_CORE_INFO("Minimal GUI host finished");
    }
    return result;
}
