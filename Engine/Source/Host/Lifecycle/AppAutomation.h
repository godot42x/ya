#pragma once

#include "Core/Api.h"
#include "RenderGraph/RenderGraph.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace ya
{

struct App;
struct AppDesc;
struct AppAutomationOptions;
struct ICommandBuffer;
struct IRender;
struct OffscreenJobQueueService;
struct RenderImage;
struct Texture;

struct AppAutomationFrameContext
{
    IRender*                                render                            = nullptr;
    std::shared_ptr<RenderImage>            postprocessImage                  = nullptr;
    std::shared_ptr<RenderImage>            viewportImage                     = nullptr;
    std::shared_ptr<RenderImage>            presentationImage                 = nullptr;
    std::function<bool()>                   requestRenderDocCapture;
    std::function<bool()>                   isRenderDocCapturePending;
    std::function<bool()>                   isRenderDocCaptureTerminal;
    std::function<const std::string&()>     getRenderDocCapturePath;
    std::function<const std::string&()>     getRenderDocPassSummaryPath;
    uint64_t                                frameIndex                        = 0;
};

class YA_HOST_API AppAutomation
{
  public:
    static bool isFrameAutomationEnabled(const App& app);
    static void loadConfig(AppDesc& appDesc);
    static void applyStartupOverrides(AppDesc& appDesc);
    static void applyLogOverrides(const AppDesc& appDesc);
    static void applyRuntimeOverrides(App& app);
    static bool shouldDeferQuit(const App& app);
    static OffscreenJobQueueService buildOffscreenJobQueueService(App& app);
    static bool appendPresentationCapture(uint64_t frameIndex,
                                          RenderGraph&    graph,
                                          RGTextureHandle presentationOutput,
                                          Extent2D        presentationExtent);
    static void onFrameCompleted(App& app, const AppAutomationFrameContext& frameContext);
};

} // namespace ya
