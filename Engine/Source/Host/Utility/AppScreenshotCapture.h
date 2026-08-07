#pragma once

#include "RenderGraph/RenderGraph.h"
#include "RHI/RenderDefines.h"
#include "Core/Api.h"

#include <cstdint>
#include <memory>
#include <string>

namespace ya
{

struct IBuffer;
struct IRender;
enum class EAutomationScreenshotTarget : uint8_t;
struct ICommandBuffer;
struct OffscreenJobQueueService;
struct OffscreenJobState;
struct RenderGraphExecutor;
struct RenderImage;
struct Texture;

struct AppScreenshotCaptureState
{
    std::string                        outputPath;
    std::shared_ptr<IBuffer>           readbackBuffer;
    std::shared_ptr<OffscreenJobState> pendingJob;
    std::shared_ptr<RenderGraphExecutor> copyExecutor;
    std::shared_ptr<RenderImage>       presentationSourceImage;
    uint32_t                           width                         = 0;
    uint32_t                           height                        = 0;
    uint64_t                           recordedFrameIndex            = 0;
    EFormat::T                         sourceFormat                  = EFormat::Undefined;
    EAutomationScreenshotTarget        target                        = static_cast<EAutomationScreenshotTarget>(0);
    bool                               bCompleted                    = false;
    bool                               bFailed                       = false;
    bool                               bPendingPresentationCapture   = false;
    bool                               bPresentationCopyRecorded     = false;
};

class ENGINE_API AppScreenshotCapture
{
  public:
    static bool request(IRender* render,
                        const OffscreenJobQueueService& offscreenQueueService,
                        std::shared_ptr<RenderImage> postprocessSourceImage,
                        std::shared_ptr<RenderImage> viewportSourceImage,
                        std::shared_ptr<RenderImage> presentationSourceImage,
                        AppScreenshotCaptureState& state,
                        const std::string& outputPath,
                        EAutomationScreenshotTarget target);
    /// Append a presentation readback copy pass to the live presentation
    /// graph, instead of recording a standalone copy outside the graph.
    static bool appendPresentationCapture(uint64_t frameIndex,
                                          AppScreenshotCaptureState& state,
                                          RenderGraph&               graph,
                                          RGTextureHandle            presentationOutput,
                                          Extent2D                   presentationExtent);
    static bool tryFinalize(uint64_t currentFrameIndex, AppScreenshotCaptureState& state);
    static void reset(AppScreenshotCaptureState& state);
};

} // namespace ya
