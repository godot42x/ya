#pragma once

#include "Core/Api.h"
#include "Graph/RenderGraph.h"

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
struct RenderTexture;
struct Texture;

struct AppAutomationTickContext
{
    IRender*                                render                            = nullptr;
    std::shared_ptr<RenderTexture>          postprocessImage                  = nullptr;
    std::shared_ptr<RenderTexture>          viewportImage                     = nullptr;
    std::shared_ptr<RenderTexture>          presentationImage                 = nullptr;
    std::function<bool()>                   requestRenderDocCapture;
    std::function<bool()>                   isRenderDocCapturePending;
    std::function<bool()>                   isRenderDocCaptureTerminal;
    std::function<const std::string&()>     getRenderDocCapturePath;
    std::function<const std::string&()>     getRenderDocPassSummaryPath;
    uint64_t                                hostTick                          = 0;
};

class YA_GAME_RUNTIME_API AppAutomation
{
  public:
    static bool isFrameAutomationEnabled(const App& app);
    static void loadConfig(AppDesc& appDesc);
    static void applyStartupOverrides(AppDesc& appDesc);
    static void applyLogOverrides(const AppDesc& appDesc);
    static void applyRuntimeOverrides(App& app);
    static bool shouldDeferQuit(const App& app);
    static OffscreenJobQueueService buildOffscreenJobQueueService(App& app);
    static bool appendPresentationCapture(uint64_t        hostTick,
                                          RenderGraph&    graph,
                                          RGTextureHandle presentationOutput,
                                          Extent2D        presentationExtent);
    static void onTickCompleted(App& app, const AppAutomationTickContext& frameContext);
};

} // namespace ya
