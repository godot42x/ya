#pragma once

#include "Core/Base.h"

#include "Core/Debug/RenderDocCapture.h"

#include <memory>
#include <string>

namespace ya
{

struct AppDesc;
struct IRender;
struct RenderDiagnosticsService
{
    struct RenderDocState
    {
        stdptr<RenderDocCapture> capture;
        int                      onCaptureAction               = 0;
        bool                     bAutomationCaptureRequested   = false;
        bool                     bAutomationCaptureFinished    = false;
        bool                     bAutomationCaptureFailed      = false;
        bool                     bAutomationPostProcessPending = false;
        std::string              lastCapturePath;
        std::string              automationPassSummaryPath;
        std::string              configuredDllPath;
        std::string              configuredOutputDir;
    };

    void init(IRender* render, const AppDesc& appDesc);
    void shutdown();

    void onFrameBegin();
    void onFrameEnd();

    [[nodiscard]] bool requestAutomationRenderDocCapture();
    [[nodiscard]] bool isAutomationRenderDocCapturePending() const;
    [[nodiscard]] bool isAutomationRenderDocCaptureTerminal() const;
    [[nodiscard]] const std::string& getAutomationRenderDocCapturePath() const;
    [[nodiscard]] const std::string& getAutomationRenderDocPassSummaryPath() const;

    [[nodiscard]] RenderDocState&       getRenderDocState() { return _renderDoc; }
    [[nodiscard]] const RenderDocState& getRenderDocState() const { return _renderDoc; }

  private:
    void configureRenderContext();
    void handleCaptureFinished(const RenderDocCapture::CaptureResult& result);

    IRender*       _render = nullptr;
    RenderDocState _renderDoc{};
};

} // namespace ya
