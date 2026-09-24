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
    /// The automation-side state machine for RenderDoc captures. Internal: the
    /// panel-facing answer is the value snapshot below, and the commands are
    /// the service's methods -- no caller needs the live capture handle.
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

    /// The diagnostics panel's answer, as a value: what RenderDoc is configured
    /// with and what the capture is doing right now. `bAvailable` means a
    /// capture context exists and the RenderDoc API reports itself usable --
    /// the panel's "can I offer these controls at all". No live handle inside,
    /// so a snapshot cannot outlive its frame meaningfully.
    struct RenderDocPanelState
    {
        bool        bAvailable      = false;
        bool        bCaptureEnabled = false;
        bool        bHUDVisible     = false;
        bool        bCapturing      = false;
        uint32_t    delayFrames     = 0;
        std::string configuredDllPath;
        std::string configuredOutputDir;
        std::string lastCapturePath;
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

    /// The panel-facing answer and commands. Commands are requests, not
    /// guarantees: without a capture context (RenderDoc disabled) they are
    /// no-ops, and the snapshot says so through `bAvailable`.
    [[nodiscard]] RenderDocPanelState buildRenderDocPanelState() const;
    void requestRenderDocCaptureEnabled(bool bEnabled);
    void requestRenderDocHUDVisible(bool bVisible);
    void requestRenderDocCaptureNextFrame();
    void requestRenderDocCaptureAfterFrames(uint32_t frameCount);

  private:
    void configureRenderContext();
    void handleCaptureFinished(const RenderDocCapture::CaptureResult& result);

    IRender*       _render = nullptr;
    IRenderSurfaceContext* _captureSurface = nullptr;
    RenderDocState _renderDoc{};
};

} // namespace ya
