#include "GameEditor/UI/RuntimeRenderSettingsSection.h"

#include "GameRuntime/App.h"
#include "GUI/Declarative/Build.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/CheckBox.h"
#include "GUI/Widgets/Controls/ComboBox.h"
#include "GUI/Widgets/Controls/DragFloat.h"
#include "GUI/Widgets/Controls/Text.h"
#include "RHI/Core/Swapchain.h"
#include "Render3D/RenderRuntime.h"

#include <format>

namespace ya
{
namespace
{
const char* pipelineLabel(int value) { return value == 0 ? "Forward" : "Deferred"; }
const char* presentLabel(int value)
{
    switch (value) {
    case EPresentMode::Immediate: return "Immediate";
    case EPresentMode::Mailbox: return "Mailbox";
    case EPresentMode::FIFO: return "FIFO";
    case EPresentMode::FIFO_Relaxed: return "FIFO Relaxed";
    default: return "Unknown";
    }
}
}

RuntimeRenderSettingsSection::RuntimeRenderSettingsSection(std::string name)
    : UICompoundWidget(std::move(name), "panel")
{
}

void RuntimeRenderSettingsSection::construct()
{
    _pipelineState = ui::text("RuntimeRenderPipelineState").share();
    _vsyncState = ui::text("RuntimeRenderVsyncState").share();
    _viewportScale = std::make_shared<UIDragFloat>("RuntimeViewportScale");
    _viewportScale->_min = 1.0f; _viewportScale->_max = 10.0f; _viewportScale->_speed = 0.1f;
    _vsync = std::make_shared<UICheckBox>("RuntimeVsync");
    _vsync->addDetachedChild(std::make_shared<UIText>("RuntimeVsyncLabel"));
    dynamic_cast<UIText*>(_vsync->getChildren().front().get())->setText("VSync");
    _presentMode = std::make_shared<UIComboBox>("RuntimePresentMode");
    _presentMode->_items = {"Immediate", "Mailbox", "FIFO", "FIFO Relaxed"};
    _reload = ui::button("RuntimeReloadPipeline")
                  .child(ui::text("RuntimeReloadPipelineLabel").setText("Reload Active Pipeline"))
                  .share();

    _viewportScale->_onValueChanged = [](float value) {
        if (auto* app = App::get()) if (auto* r = app->getRenderServices().getRenderRuntime()) r->setViewportFrameBufferScale(value);
    };
    _vsync->_onChanged = [](bool value) {
        if (auto* app = App::get()) if (auto* render = app->getRenderServices().getRender()) if (auto* sc = render->getSwapchain()) sc->setVsync(value);
    };
    _presentMode->_onSelectionChanged = [](int value) {
        if (auto* app = App::get()) if (auto* render = app->getRenderServices().getRender()) if (auto* sc = render->getSwapchain()) {
            const auto mode = static_cast<EPresentMode::T>(value);
            app->getTaskManager().registerFrameTask([sc, mode]() { sc->setPresentMode(mode); });
        }
    };
    _reload->_onClick = []() {
        if (auto* app = App::get()) if (auto* r = app->getRenderServices().getRenderRuntime()) r->requestActivePipelineReload();
    };

    auto rows = ui::column("RuntimeRenderSettingsRows")
                    .setSpacing(4.0f)
                    .child(ui::text("RuntimeRenderSettingsHeader").setText("Render Settings").setStyleKey("text.header"))
                    .child(_pipelineState)
                    .child(_viewportScale, FBoxSlotArgs{.preferredSize = {180.0f, 22.0f}})
                    .child(_vsync)
                    .child(_vsyncState)
                    .child(_presentMode, FBoxSlotArgs{.preferredSize = {180.0f, 26.0f}})
                    .child(_reload, FBoxSlotArgs{.preferredSize = {190.0f, 26.0f}});
    addDetachedChild(rows.release());
}

void RuntimeRenderSettingsSection::sync(const App* app)
{
    if (!app || !_pipelineState) return;
    auto* runtime = app->getRenderServices().getRenderRuntime();
    if (!runtime) return;
    const int pipeline = static_cast<int>(runtime->getPendingRenderPipeline());
    const bool pending = runtime->getPendingRenderPipeline() != runtime->getRenderPipeline();
    _pipelineState->setText(std::format("Pipeline: {}{}", pipelineLabel(pipeline), pending ? " (switch pending)" : ""));
    _viewportScale->setValue(runtime->getViewportFrameBufferScale(), false);
    if (auto* render = app->getRenderServices().getRender()) if (auto* sc = render->getSwapchain()) {
        _vsync->setChecked(sc->getVsync());
        _vsyncState->setText(std::format("Present Mode: {}", presentLabel(static_cast<int>(sc->getPresentMode()))));
        _presentMode->setSelectedIndex(static_cast<int>(sc->getPresentMode()), false);
    }
}
}
