#pragma once

#include "Render/RenderDefines.h"

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
struct RenderImage;
struct Texture;

struct AppScreenshotCaptureState
{
    std::string                        outputPath;
    std::shared_ptr<IBuffer>           readbackBuffer;
    std::shared_ptr<OffscreenJobState> pendingJob;
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

class AppScreenshotCapture
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
    static bool recordPresentationCapture(uint64_t frameIndex,
                                          AppScreenshotCaptureState& state,
                                          ICommandBuffer* cmdBuf);
    static bool tryFinalize(uint64_t currentFrameIndex, AppScreenshotCaptureState& state);
    static void reset(AppScreenshotCaptureState& state);
};

} // namespace ya
