#pragma once

#include "Core/Base.h"

#include "Core/Debug/RenderDocCapture.h"

#include <memory>
#include <string>

namespace ya
{


struct IRender;
struct IRenderSurfaceContext;
struct YA_RENDER_3D_API RenderDiagnosticsService
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

    /// `captureSurface` is the window this service captures and configures a
    /// RenderDoc render context for. It is an input rather than something the
    /// service looks up, because "which window am I diagnosing" is a fact about
    /// the host, not a property the renderer can infer (it used to reach for
    /// `IRender::primarySwapchain()`, i.e. assume there is one window that
    /// matters). May be null when there is no window to capture; the service
    /// then records nothing into the capture context.
    void init(IRender* render, IRenderSurfaceContext* captureSurface, bool bEnableRenderDoc,
              const std::string& renderDocDllPath,
              const std::string& renderDocCaptureOutputDir);
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
    IRenderSurfaceContext* _captureSurface = nullptr;
    RenderDocState _renderDoc{};
};

} // namespace ya
