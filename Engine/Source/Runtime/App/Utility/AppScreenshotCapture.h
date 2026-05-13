#pragma once

#include "Render/RenderDefines.h"

#include <cstdint>
#include <memory>
#include <string>

namespace ya
{

struct App;
struct IBuffer;
enum class EAutomationScreenshotTarget : uint8_t;
struct ICommandBuffer;
struct OffscreenJobState;

struct AppScreenshotCaptureState
{
    std::string                        outputPath;
    std::shared_ptr<IBuffer>           readbackBuffer;
    std::shared_ptr<OffscreenJobState> pendingJob;
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
    static bool request(App& app,
                        AppScreenshotCaptureState& state,
                        const std::string& outputPath,
                        EAutomationScreenshotTarget target);
    static bool recordPresentationCapture(App& app, AppScreenshotCaptureState& state, ICommandBuffer* cmdBuf);
    static bool tryFinalize(App& app, AppScreenshotCaptureState& state);
    static void reset(AppScreenshotCaptureState& state);
};

} // namespace ya
