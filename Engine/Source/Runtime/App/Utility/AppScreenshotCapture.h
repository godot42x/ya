#pragma once

#include "Render/RenderDefines.h"

#include <memory>
#include <string>

namespace ya
{

struct App;
struct IBuffer;
struct OffscreenJobState;

struct AppScreenshotCaptureState
{
    std::string                        outputPath;
    std::shared_ptr<IBuffer>           readbackBuffer;
    std::shared_ptr<OffscreenJobState> pendingJob;
    uint32_t                           width        = 0;
    uint32_t                           height       = 0;
    EFormat::T                         sourceFormat = EFormat::Undefined;
    bool                               bCompleted   = false;
    bool                               bFailed      = false;
};

class AppScreenshotCapture
{
  public:
    static bool request(App& app, AppScreenshotCaptureState& state, const std::string& outputPath);
    static bool tryFinalize(AppScreenshotCaptureState& state);
    static void reset(AppScreenshotCaptureState& state);
};

} // namespace ya
