#pragma once

#include "Graph/RenderGraph.h"
#include "App/Control/AutomationControlServer.h"
#include "Host/AppOptions.h"
#include "Host/Utility/AppScreenshotCapture.h"

#include <memory>
#include <optional>

namespace ya
{

struct App;
struct ICommandBuffer;
struct IRender;
struct RenderImage;

class YA_HOST_API AppAutomationControlService
{
  public:
    AppAutomationControlService();
    AppAutomationControlService(const AppAutomationControlService&) = delete;
    AppAutomationControlService& operator=(const AppAutomationControlService&) = delete;
    ~AppAutomationControlService();

    bool init(uint16_t port);
    void shutdown();

    [[nodiscard]] bool isEnabled() const { return _server.isEnabled(); }
    [[nodiscard]] uint16_t getPort() const { return _server.getPort(); }

    void update(App& app);
    void onFrameCompleted(App& app,
                          IRender* render,
                          std::shared_ptr<RenderImage> postprocessImage,
                          std::shared_ptr<RenderImage> viewportImage,
                          std::shared_ptr<RenderImage> presentationImage,
                          uint64_t frameIndex);
    bool appendPresentationCapture(uint64_t frameIndex,
                                   RenderGraph&    graph,
                                   RGTextureHandle presentationOutput,
                                   Extent2D        presentationExtent);

  private:
    struct ScreenshotRequest
    {
        AppAutomationControlServer::RequestPtr waiter;
        std::string                            outputPath;
        EAutomationScreenshotTarget            target = EAutomationScreenshotTarget::Viewport;
        uint64_t                               earliestFrameIndex = 0;
        AppScreenshotCaptureState              state;
    };

    void handleCall(App& app, const AppAutomationControlServer::RequestPtr& call);
    void handlePing(const AppAutomationControlServer::RequestPtr& call);
    void handleGetPointLightPos(App& app, const AppAutomationControlServer::RequestPtr& call);
    void handleGetDirectionalLightInfo(App& app, const AppAutomationControlServer::RequestPtr& call);
    void handleSetRenderPipeline(App& app, const AppAutomationControlServer::RequestPtr& call);
    void handleSetShadowSettings(App& app, const AppAutomationControlServer::RequestPtr& call);
    void handleSetAppState(App& app, const AppAutomationControlServer::RequestPtr& call);
    void handleSetEditorCamera(App& app, const AppAutomationControlServer::RequestPtr& call);
    void handleCaptureScreenshot(App& app, const AppAutomationControlServer::RequestPtr& call);
    void handleQuit(App& app, const AppAutomationControlServer::RequestPtr& call);
    void handleGetWorldViewState(App& app, const AppAutomationControlServer::RequestPtr& call);
    void handleListOverlaySprites(App& app, const AppAutomationControlServer::RequestPtr& call);
    void handleListBillboardComponents(App& app, const AppAutomationControlServer::RequestPtr& call);
    void handleListSceneEntities(App& app, const AppAutomationControlServer::RequestPtr& call);
    void handleGetEntityInfo(App& app, const AppAutomationControlServer::RequestPtr& call);
    void handleFindEntitiesNear(App& app, const AppAutomationControlServer::RequestPtr& call);
    void handleCreateBillboardRegressionScene(App& app, const AppAutomationControlServer::RequestPtr& call);
    void handleSetEditorConfigValue(App& app, const AppAutomationControlServer::RequestPtr& call);
    void handleEntityRemoveComponent(App& app, const AppAutomationControlServer::RequestPtr& call);
    void handleEntitySetMeshVisible(App& app, const AppAutomationControlServer::RequestPtr& call);
    void handleEvalJS(App& app, const AppAutomationControlServer::RequestPtr& call);
    void handleInvoke(App& app, const AppAutomationControlServer::RequestPtr& call);
    void handleListCommands(App& app, const AppAutomationControlServer::RequestPtr& call);

    void completeCall(const AppAutomationControlServer::RequestPtr& call, nlohmann::json response);
    [[nodiscard]] nlohmann::json makeSuccess(const AppAutomationControlServer::Request& call,
                                             nlohmann::json result = nlohmann::json::object()) const;
    [[nodiscard]] nlohmann::json makeError(const AppAutomationControlServer::Request& call, std::string_view message) const;

    AppAutomationControlServer      _server;
    std::optional<ScreenshotRequest> _pendingScreenshot;
};

} // namespace ya
