#include "GameEditor/UI/RuntimeDiagnosticsSection.h"

#include "GameRuntime/App.h"
#include "GUI/Declarative/Build.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/CheckBox.h"
#include "GUI/Widgets/WidgetTree.h"
#include "Render3D/RenderDeviceState.h"
#include "Render3D/Services/RenderDiagnosticsService.h"

#include <format>

namespace ya
{

RuntimeDiagnosticsSection::RuntimeDiagnosticsSection(std::string name)
    : UICompoundWidget(std::move(name), "panel")
{
}

void RuntimeDiagnosticsSection::construct()
{
    _availability = ui::text("RuntimeDiagnosticsAvailability")
                        .setStyleKey("text.muted")
                        .share();
    _dllPath = ui::text("RuntimeDiagnosticsDllPath").share();
    _outputDir = ui::text("RuntimeDiagnosticsOutputDir").share();
    _lastCapture = ui::text("RuntimeDiagnosticsLastCapture").share();
    _captureState = ui::text("RuntimeDiagnosticsCaptureState").share();
    _captureEnabled = std::make_shared<UICheckBox>("RuntimeDiagnosticsCaptureEnabled");
    _captureEnabled->addDetachedChild(std::make_shared<UIText>("RuntimeDiagnosticsCaptureEnabledLabel"));
    if (auto* label = dynamic_cast<UIText*>(_captureEnabled->getChildren().front().get())) label->setText("Capture Enabled");
    _hudVisible = std::make_shared<UICheckBox>("RuntimeDiagnosticsHudVisible");
    _hudVisible->addDetachedChild(std::make_shared<UIText>("RuntimeDiagnosticsHudVisibleLabel"));
    if (auto* label = dynamic_cast<UIText*>(_hudVisible->getChildren().front().get())) label->setText("Show RenderDoc HUD");
    _captureNextFrame = ui::button("RuntimeDiagnosticsCaptureNext")
                            .child(ui::text("RuntimeDiagnosticsCaptureNextLabel").setText("Capture Next Frame"))
                            .share();
    _captureAfterFrames = ui::button("RuntimeDiagnosticsCaptureAfter")
                              .child(ui::text("RuntimeDiagnosticsCaptureAfterLabel").setText("Capture After 120 Frames"))
                              .share();
    _captureEnabled->_onChanged = [](bool value) {
        if (auto* app = App::get()) {
            if (auto* runtime = app->getRenderServices().getDeviceState()) {
                auto& state = runtime->getDiagnosticsService().getRenderDocState();
                if (state.capture) state.capture->setCaptureEnabled(value);
            }
        }
    };
    _hudVisible->_onChanged = [](bool value) {
        if (auto* app = App::get()) {
            if (auto* runtime = app->getRenderServices().getDeviceState()) {
                auto& state = runtime->getDiagnosticsService().getRenderDocState();
                if (state.capture) state.capture->setHUDVisible(value);
            }
        }
    };
    _captureNextFrame->_onClick = []() {
        if (auto* app = App::get()) if (auto* runtime = app->getRenderServices().getDeviceState()) {
            auto& state = runtime->getDiagnosticsService().getRenderDocState();
            if (state.capture && state.capture->isCaptureEnabled()) state.capture->requestNextFrame();
        }
    };
    _captureAfterFrames->_onClick = []() {
        if (auto* app = App::get()) if (auto* runtime = app->getRenderServices().getDeviceState()) {
            auto& state = runtime->getDiagnosticsService().getRenderDocState();
            if (state.capture && state.capture->isCaptureEnabled()) state.capture->requestAfterFrames(120);
        }
    };

    auto rows = ui::column("RuntimeDiagnosticsRows")
                    .setSpacing(3.0f)
                    .child(ui::text("RuntimeDiagnosticsHeader")
                               .setText("Diagnostics")
                               .setStyleKey("text.header"))
                    .child(_availability)
                    .child(_dllPath)
                    .child(_outputDir)
                    .child(_lastCapture)
                    .child(_captureState);
    rows.child(_captureEnabled)
        .child(_hudVisible)
        .child(_captureNextFrame, FBoxSlotArgs{.preferredSize = {180.0f, 24.0f}})
        .child(_captureAfterFrames, FBoxSlotArgs{.preferredSize = {180.0f, 24.0f}});
    addDetachedChild(rows.release());
    sync(nullptr);
}

void RuntimeDiagnosticsSection::sync(const App* app)
{
    if (!_availability) {
        return;
    }

    const auto setUnavailable = [this](std::string reason)
    {
        _availability->setText(std::move(reason));
        _dllPath->setText("DLL Path: <unavailable>");
        _outputDir->setText("Output Dir: <unavailable>");
        _lastCapture->setText("Last Capture: <none>");
        _captureState->setText("Capture: unavailable");
    };

    if (!app) {
        setUnavailable("RenderDoc: runtime unavailable");
        return;
    }
    auto* runtime = app->getRenderServices().getDeviceState();
    if (!runtime) {
        setUnavailable("RenderDoc: render runtime unavailable");
        return;
    }

    const auto& state = runtime->getDiagnosticsService().getRenderDocState();
    const bool available = state.capture && state.capture->isAvailable();
    _availability->setText(std::format("RenderDoc: {}", available ? "Available" : "Unavailable"));
    _dllPath->setText(std::format("DLL Path: {}", state.configuredDllPath.empty() ? "<default>" : state.configuredDllPath));
    _outputDir->setText(std::format("Output Dir: {}", state.configuredOutputDir.empty() ? "<default>" : state.configuredOutputDir));
    _lastCapture->setText(std::format("Last Capture: {}", state.lastCapturePath.empty() ? "<none>" : state.lastCapturePath));
    if (!state.capture) {
        _captureState->setText("Capture: unavailable");
        _captureEnabled->setEnabled(false);
        _hudVisible->setEnabled(false);
        _captureNextFrame->setEnabled(false);
        _captureAfterFrames->setEnabled(false);
    }
    else {
        _captureEnabled->setEnabled(available);
        _hudVisible->setEnabled(available);
        _captureNextFrame->setEnabled(available && state.capture->isCaptureEnabled());
        _captureAfterFrames->setEnabled(available && state.capture->isCaptureEnabled());
        _captureEnabled->setChecked(state.capture->isCaptureEnabled());
        _hudVisible->setChecked(state.capture->isHUDVisible());
        _captureState->setText(std::format("Capture: {} | queued delay: {} frame(s)",
                                            state.capture->isCapturing() ? "active" : "idle",
                                            state.capture->getDelayFrames()));
    }
}

} // namespace ya
