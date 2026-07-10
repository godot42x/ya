#pragma once

#include <string>

namespace ya
{

struct App;
struct AppDesc;
struct AppAutomationOptions;
struct ICommandBuffer;
struct OffscreenJobQueueService;
struct RenderRuntimeFrameServices;
struct Texture;

class AppAutomation
{
  public:
    static bool isFrameAutomationEnabled(const App& app);
    static void loadConfig(AppDesc& appDesc);
    static void applyStartupOverrides(AppDesc& appDesc);
    static void applyRuntimeOverrides(App& app);
    static bool shouldDeferQuit(const App& app);
    static bool requestRenderDocCapture(const RenderRuntimeFrameServices& services);
    static bool isRenderDocCapturePending(const RenderRuntimeFrameServices& services);
    static bool isRenderDocCaptureTerminal(const RenderRuntimeFrameServices& services);
    static const std::string& getRenderDocCapturePath(const RenderRuntimeFrameServices& services);
    static const std::string& getRenderDocPassSummaryPath(const RenderRuntimeFrameServices& services);
    static Texture* getViewportScreenshotTexture(const RenderRuntimeFrameServices& services, Texture* postprocessTexture);
    static Texture* getPresentationScreenshotTexture(const RenderRuntimeFrameServices& services);
    static OffscreenJobQueueService buildOffscreenJobQueueService(App& app);
    static void recordPresentationCapture(App& app, ICommandBuffer* cmdBuf);
    static void onFrameCompleted(App& app);
};

} // namespace ya
