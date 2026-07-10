#pragma once

#include <string>

namespace ya
{

struct App;
struct AppDesc;
struct AppAutomationOptions;
struct ICommandBuffer;
struct IRender;
struct OffscreenJobQueueService;
struct RenderRuntimeFrameServices;
struct Texture;

struct AppAutomationFrameContext
{
    IRender*                        render              = nullptr;
    Texture*                        postprocessTexture  = nullptr;
    Texture*                        presentationTexture = nullptr;
    const RenderRuntimeFrameServices* frameServices     = nullptr;
    uint64_t                        frameIndex          = 0;
};

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
    static void recordPresentationCapture(Texture* presentationSourceTexture,
                                          uint64_t frameIndex,
                                          ICommandBuffer* cmdBuf);
    static void onFrameCompleted(App& app, const AppAutomationFrameContext& frameContext);
};

} // namespace ya
