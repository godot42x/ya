#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace ya
{


struct RenderDocCapture
{
    struct CaptureResult
    {
        bool        bSuccess = false;
        std::string capturePath;
        uint64_t    timestamp = 0;
    };

    using CaptureFinishedCallback = std::function<void(const CaptureResult&)>;

    struct RenderContext
    {
        void* device    = nullptr;
        void* swapchain = nullptr;
    };

  private:
    void*         _module = nullptr;
    void*         _api    = nullptr;
    RenderContext _renderContext{};
    RenderContext _activeCaptureContext{};

    bool     _available     = false;
    bool     _captureEnabled = true;
    bool     _hudVisible     = true;
    bool     _captureQueued = false;
    bool     _capturing     = false;
    uint32_t _delayFrames   = 0;

    std::string             _captureOutputDir;
    std::string             _lastCapturePath;
    CaptureFinishedCallback _onCaptureFinished;

  public:


    bool init(const std::string& dllPath = "renderdoc.dll", const std::string& captureOutputDir = "");
    void shutdown();

    bool     isAvailable() const { return _available; }
    bool     isCaptureEnabled() const { return _captureEnabled; }
    bool     isHUDVisible() const { return _hudVisible; }
    bool     isCapturing() const { return _capturing; }
    uint32_t getDelayFrames() const { return _delayFrames; }

    void setCaptureEnabled(bool enabled);
    void setHUDVisible(bool visible);

    void requestNextFrame();
    void requestAfterFrames(uint32_t frames);

    void setCaptureFinishedCallback(CaptureFinishedCallback callback) { _onCaptureFinished = std::move(callback); }

    const std::string& getLastCapturePath() const { return _lastCapturePath; }
    const std::string& getCaptureOutputDir() const { return _captureOutputDir; }
    bool               launchReplayUI(bool bConnectTargetControl = true, const char* cmdLine = nullptr);

    void onFrameBegin();
    void onFrameEnd();

    void setRenderContext(const RenderContext& renderContext) { _renderContext = renderContext; }
};

} // namespace ya
