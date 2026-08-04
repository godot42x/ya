#pragma once

#include "Runtime/Application/AppOptions.h"
#include "Runtime/Application/Utility/AppScreenshotCapture.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

namespace ya
{

struct App;
struct ICommandBuffer;
struct IRender;
struct RenderImage;

class ENGINE_API AppAutomationControlService
{
  public:
    AppAutomationControlService() = default;
    AppAutomationControlService(const AppAutomationControlService&) = delete;
    AppAutomationControlService& operator=(const AppAutomationControlService&) = delete;
    ~AppAutomationControlService();

    bool init(uint16_t port);
    void shutdown();

    [[nodiscard]] bool isEnabled() const { return _bEnabled; }
    [[nodiscard]] uint16_t getPort() const { return _port; }

    void update(App& app);
    void onFrameCompleted(App& app,
                          IRender* render,
                          std::shared_ptr<RenderImage> postprocessImage,
                          std::shared_ptr<RenderImage> viewportImage,
                          std::shared_ptr<RenderImage> presentationImage,
                          uint64_t frameIndex);
    void recordPresentationCapture(uint64_t frameIndex, ICommandBuffer* cmdBuf);

  private:
    struct ServerState;

    struct PendingCall
    {
        nlohmann::json id = nullptr;
        std::string    method;
        nlohmann::json params = nlohmann::json::object();

        std::mutex              mutex;
        std::condition_variable cv;
        bool                    bCompleted = false;
        nlohmann::json          response;
    };

    struct ScreenshotRequest
    {
        std::shared_ptr<PendingCall> waiter;
        std::string                  outputPath;
        EAutomationScreenshotTarget  target = EAutomationScreenshotTarget::Viewport;
        uint64_t                     earliestFrameIndex = 0;
        AppScreenshotCaptureState    state;
    };

    bool enqueueCall(std::shared_ptr<PendingCall> call);
    void listenerMain();
    [[nodiscard]] nlohmann::json processRpcLine(const std::string& line);

    void handleCall(App& app, const std::shared_ptr<PendingCall>& call);
    void handlePing(const std::shared_ptr<PendingCall>& call);
    void handleGetPointLightPos(App& app, const std::shared_ptr<PendingCall>& call);
    void handleGetDirectionalLightInfo(App& app, const std::shared_ptr<PendingCall>& call);
    void handleSetRenderPipeline(App& app, const std::shared_ptr<PendingCall>& call);
    void handleSetShadowSettings(App& app, const std::shared_ptr<PendingCall>& call);
    void handleSetEditorCamera(App& app, const std::shared_ptr<PendingCall>& call);
    void handleCaptureScreenshot(App& app, const std::shared_ptr<PendingCall>& call);
    void handleQuit(App& app, const std::shared_ptr<PendingCall>& call);
    void handleGetWorldViewState(App& app, const std::shared_ptr<PendingCall>& call);
    void handleListOverlaySprites(App& app, const std::shared_ptr<PendingCall>& call);
    void handleListBillboardComponents(App& app, const std::shared_ptr<PendingCall>& call);
    void handleListSceneEntities(App& app, const std::shared_ptr<PendingCall>& call);
    void handleGetEntityInfo(App& app, const std::shared_ptr<PendingCall>& call);
    void handleFindEntitiesNear(App& app, const std::shared_ptr<PendingCall>& call);
    void handleCreateBillboardRegressionScene(App& app, const std::shared_ptr<PendingCall>& call);
    void handleSetEditorConfigValue(App& app, const std::shared_ptr<PendingCall>& call);
    void handleEntityRemoveComponent(App& app, const std::shared_ptr<PendingCall>& call);
    void handleEntitySetMeshVisible(App& app, const std::shared_ptr<PendingCall>& call);

    void completeCall(const std::shared_ptr<PendingCall>& call, nlohmann::json response);
    [[nodiscard]] nlohmann::json makeSuccess(const PendingCall& call, nlohmann::json result = nlohmann::json::object()) const;
    [[nodiscard]] nlohmann::json makeError(const PendingCall& call, std::string_view message) const;

    std::atomic<bool> _bEnabled = false;
    std::atomic<bool> _bStopRequested = false;
    uint16_t          _port = 0;
    std::thread       _listenerThread;
    std::unique_ptr<ServerState> _serverState;

    std::mutex                               _incomingMutex;
    std::deque<std::shared_ptr<PendingCall>> _incomingCalls;
    std::optional<ScreenshotRequest>         _pendingScreenshot;
};

} // namespace ya
