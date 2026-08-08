#include "Host/Automation/AppAutomationControlService.h"

#include "Host/App.h"
#include "Gameplay/Systems/JSScriptingSystem.h"
#include "Host/AppSceneServices.h"
#include "Host/Automation/EditorAutomationControl.h"
#include "Host/Lifecycle/AppAutomation.h"
#include "Host/Utility/OffscreenJobRunner.h"

#include "Host/Config/ConfigManager.h"

#include "Core/Log.h"
#include "Core/Math/Geometry.h"
#include "ECS/ECSRegistry.h"

#include "ECS/Component/2D/BillboardComponent.h"
#include "ECS/Component/DirectionalLightComponent.h"
#include "ECS/Component/Mesh/StaticMeshComponent.h"
#include "ECS/Component/ModelComponent.h"
#include "ECS/Component/PointLightComponent.h"
#include "ECS/Component/RenderComponent.h"
#include "Scene3D/TransformComponent.h"
#include "ECS/Component/CameraComponent.h"
#include "Render3D/Adapters/LightBillboard/LightBillboardLinkageRule.h"
#include "Gameplay/Systems/TransformSystem.h"

#include "Render3D/RenderRuntime.h"
#include "Scene/Core/Scene.h"
#include "Scene/Runtime/SceneManager.h"

#include <asio.hpp>
#include <cmath>
#include <filesystem>
#include <istream>
#include <string_view>

namespace ya
{
struct AppAutomationControlService::ServerState
{
    asio::io_context                         ioContext;
    std::unique_ptr<asio::ip::tcp::acceptor> acceptor;
};

namespace
{
using asio::ip::tcp;

struct DirectionalLightInfo
{
    glm::vec3                direction = glm::vec3(0.0f, -1.0f, 0.0f);
    glm::vec3                color     = glm::vec3(1.0f);
    float                    intensity = 1.0f;
    std::optional<glm::vec3> position;
};

std::optional<glm::vec3> parseVec3Param(const nlohmann::json& value)
{
    if (value.is_array() && value.size() == 3) {
        return glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
    }
    if (value.is_object() && value.contains("x") && value.contains("y") && value.contains("z")) {
        return glm::vec3(value["x"].get<float>(), value["y"].get<float>(), value["z"].get<float>());
    }
    return std::nullopt;
}

std::optional<EAutomationScreenshotTarget> parseScreenshotTarget(const nlohmann::json& params)
{
    if (!params.contains("target") || params["target"].is_null()) {
        return EAutomationScreenshotTarget::Viewport;
    }

    const std::string           text   = params["target"].get<std::string>();
    EAutomationScreenshotTarget target = EAutomationScreenshotTarget::Viewport;
    if (!tryParseAutomationScreenshotTarget(text, target)) {
        return std::nullopt;
    }
    return target;
}

IEditorAutomationControl* getEditorAutomationControl(App& app)
{
    return app.queryModuleInterface<IEditorAutomationControl>(YA_EDITOR_AUTOMATION_CONTROL_INTERFACE);
}

Scene* resolveControlScene(App& app)
{
    if (auto* editorControl = getEditorAutomationControl(app)) {
        if (Scene* authoringScene = editorControl->getAuthoringScene()) {
            return authoringScene;
        }
    }
    return app.getSceneServices().getActiveScene();
}

std::vector<std::string> enumerateComponentNames(const entt::registry& registry, entt::entity entity)
{
    std::vector<std::string> names;
    const auto& cache = ECSRegistry::get().getTypeIndexCache();
    for (const auto& [fname, typeIndex] : cache) {
        if (ECSRegistry::get().hasComponent(typeIndex, registry, entity)) {
            names.push_back(fname.c_str());
        }
    }
    return names;
}

nlohmann::json enumerateComponentDetails(const entt::registry& registry, entt::entity entity)
{
    nlohmann::json components = nlohmann::json::array();
    const auto& cache = ECSRegistry::get().getTypeIndexCache();
    for (const auto& [fname, typeIndex] : cache) {
        if (ECSRegistry::get().hasComponent(typeIndex, registry, entity)) {
            components.push_back(fname.c_str());
        }
    }
    return components;
}

std::optional<glm::vec3> findFirstPointLightPosition(Scene* scene)
{
    if (!scene) {
        return std::nullopt;
    }

    auto view = scene->getRegistry().view<PointLightComponent, TransformComponent>();
    for (const auto& [entity, light, transform] : view.each()) {
        (void)entity;
        if (light.intensity <= 0.0f) {
            continue;
        }
        return transform.getWorldPosition();
    }

    return std::nullopt;
}

std::optional<DirectionalLightInfo> findFirstDirectionalLightInfo(Scene* scene)
{
    if (!scene) {
        return std::nullopt;
    }

    auto transformedView = scene->getRegistry().view<DirectionalLightComponent, TransformComponent>();
    for (const auto& [entity, light, transform] : transformedView.each()) {
        (void)entity;
        if (!light.bEnable || light.intensity <= 0.0f) {
            continue;
        }

        TransformSystem::computeWorldMatrix(&transform);
        glm::vec3 direction = transform.getForward();
        if (glm::length2(direction) <= std::numeric_limits<float>::epsilon()) {
            direction = light._direction;
        }

        return DirectionalLightInfo{
            .direction = glm::normalize(direction),
            .color     = light._color,
            .intensity = light.intensity,
            .position  = transform.getWorldPosition(),
        };
    }

    auto view = scene->getRegistry().view<DirectionalLightComponent>();
    for (const auto& [entity, light] : view.each()) {
        (void)entity;
        if (!light.bEnable || light.intensity <= 0.0f) {
            continue;
        }

        return DirectionalLightInfo{
            .direction = glm::normalize(light._direction),
            .color     = light._color,
            .intensity = light.intensity,
        };
    }

    return std::nullopt;
}

Scene* createBillboardRegressionScene(App& app)
{
    auto* sceneManager = app.getSceneServices().getSceneManager();
    if (!sceneManager) {
        return nullptr;
    }

    auto scene = makeShared<Scene>("BillboardRegression");
    Scene* rawScene = scene.get();
    if (!sceneManager->activateScene(scene)) {
        return nullptr;
    }

    auto* cameraNode = rawScene->createNode3D("RegressionCamera");
    if (cameraNode) {
        auto* cameraEntity = cameraNode->getEntity();
        auto* transform = cameraEntity->getComponent<TransformComponent>();
        auto* camera = cameraEntity->addComponent<CameraComponent>();
        if (transform) {
            transform->setPosition(glm::vec3(0.0f, 2.5f, 8.0f));
            transform->setRotation(glm::vec3(-10.0f, 180.0f, 0.0f));
        }
        if (camera) {
            camera->bPrimary = true;
            camera->_nearClip = 0.1f;
            camera->_farClip = 200.0f;
        }
    }

    auto* pointNode = rawScene->createNode3D("RegressionPointLight");
    if (pointNode) {
        auto* entity = pointNode->getEntity();
        auto* transform = entity->getComponent<TransformComponent>();
        auto* light = entity->addComponent<PointLightComponent>();
        if (transform) {
            transform->setPosition(glm::vec3(0.0f, 1.5f, 0.0f));
            transform->setScale(glm::vec3(1.0f));
        }
        if (light) {
            light->color = glm::vec3(1.0f, 0.8f, 0.35f);
            light->intensity = 6.0f;
        }
        LightBillboardLinkageRule::applyLinkage(rawScene, entity->getHandle());
    }

    auto* directionalNode = rawScene->createNode3D("RegressionDirectionalLight");
    if (directionalNode) {
        auto* entity = directionalNode->getEntity();
        auto* transform = entity->getComponent<TransformComponent>();
        auto* light = entity->addComponent<DirectionalLightComponent>();
        if (transform) {
            transform->setPosition(glm::vec3(2.0f, 3.5f, -1.0f));
            transform->setRotation(glm::vec3(-35.0f, 45.0f, 0.0f));
        }
        if (light) {
            light->_color = glm::vec3(1.0f, 0.97f, 0.8f);
            light->intensity = 3.0f;
            light->bEnable = true;
        }
        LightBillboardLinkageRule::applyLinkage(rawScene, entity->getHandle());
    }

    app.getSceneServices().refreshSceneDerivedState(rawScene);
    return rawScene;
}

} // namespace

AppAutomationControlService::AppAutomationControlService() = default;

AppAutomationControlService::~AppAutomationControlService()
{
    shutdown();
}

bool AppAutomationControlService::init(uint16_t port)
{
    shutdown();
    if (port == 0) {
        return true;
    }

    _port           = port;
    _bStopRequested = false;
    _bEnabled       = true;
    _serverState    = std::make_unique<ServerState>();
    _listenerThread = std::thread([this]()
                                  { listenerMain(); });
    YA_CORE_INFO("Automation control server listening on 127.0.0.1:{}", _port);
    return true;
}

void AppAutomationControlService::shutdown()
{
    _bStopRequested = true;
    _bEnabled       = false;

    if (_serverState) {
        asio::error_code ec;
        if (_serverState->acceptor) {
            _serverState->acceptor->cancel(ec);
            _serverState->acceptor->close(ec);
        }
        _serverState->ioContext.stop();
    }

    if (_listenerThread.joinable()) {
        _listenerThread.join();
    }

    std::deque<std::shared_ptr<PendingCall>> pending;
    {
        std::scoped_lock lock(_incomingMutex);
        pending.swap(_incomingCalls);
    }

    for (auto& call : pending) {
        completeCall(call,
                     {
                         {"id", call->id},
                         {"ok", false},
                         {"error", "automation control server shutting down"},
                     });
    }

    if (_pendingScreenshot) {
        AppScreenshotCapture::reset(_pendingScreenshot->state);
        completeCall(_pendingScreenshot->waiter,
                     {
                         {"id", _pendingScreenshot->waiter->id},
                         {"ok", false},
                         {"error", "screenshot request canceled during shutdown"},
                     });
        _pendingScreenshot.reset();
    }

    _port = 0;
    _serverState.reset();
}

bool AppAutomationControlService::enqueueCall(std::shared_ptr<PendingCall> call)
{
    if (!_bEnabled || _bStopRequested) {
        return false;
    }

    std::scoped_lock lock(_incomingMutex);
    _incomingCalls.push_back(std::move(call));
    return true;
}

void AppAutomationControlService::listenerMain()
{
    if (!_serverState) {
        YA_CORE_ERROR("Automation control server missing runtime state");
        _bEnabled = false;
        return;
    }

    auto&            serverState = *_serverState;
    asio::error_code ec;
    const auto       address = asio::ip::make_address("127.0.0.1", ec);
    if (ec) {
        YA_CORE_ERROR("Automation control server failed to parse listen address: {}", ec.message());
        _bEnabled = false;
        return;
    }

    const tcp::endpoint endpoint(address, _port);
    serverState.acceptor = std::make_unique<tcp::acceptor>(serverState.ioContext);
    serverState.acceptor->open(endpoint.protocol(), ec);
    if (ec) {
        YA_CORE_ERROR("Automation control server failed to open acceptor: {}", ec.message());
        _bEnabled = false;
        return;
    }

    serverState.acceptor->set_option(tcp::acceptor::reuse_address(true), ec);
    if (ec) {
        YA_CORE_WARN("Automation control server failed to set reuse_address: {}", ec.message());
    }

    serverState.acceptor->bind(endpoint, ec);
    if (ec) {
        YA_CORE_ERROR("Automation control server failed to bind port {}: {}", _port, ec.message());
        _bEnabled = false;
        return;
    }

    serverState.acceptor->listen(asio::socket_base::max_listen_connections, ec);
    if (ec) {
        YA_CORE_ERROR("Automation control server failed to listen on port {}: {}", _port, ec.message());
        _bEnabled = false;
        return;
    }

    while (!_bStopRequested) {
        tcp::socket clientSocket(serverState.ioContext);
        serverState.acceptor->accept(clientSocket, ec);
        if (ec) {
            if (_bStopRequested || ec == asio::error::operation_aborted) {
                break;
            }
            continue;
        }

        asio::streambuf requestBuffer;
        while (!_bStopRequested) {
            const size_t bytes = asio::read_until(clientSocket, requestBuffer, '\n', ec);
            if (ec) {
                break;
            }
            if (bytes == 0) {
                continue;
            }

            std::istream input(&requestBuffer);
            std::string  line;
            std::getline(input, line);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.empty()) {
                continue;
            }

            const nlohmann::json response = processRpcLine(line);
            const std::string    payload  = response.dump() + "\n";
            asio::write(clientSocket, asio::buffer(payload), ec);
            if (ec) {
                break;
            }
        }
    }
}

nlohmann::json AppAutomationControlService::processRpcLine(const std::string& line)
{
    nlohmann::json request;
    try {
        request = nlohmann::json::parse(line);
    }
    catch (const std::exception& e) {
        return {
            {"id", nullptr},
            {"ok", false},
            {"error", std::string("invalid json: ") + e.what()},
        };
    }

    if (!request.is_object() || !request.contains("method") || !request["method"].is_string()) {
        return {
            {"id", request.value("id", nlohmann::json(nullptr))},
            {"ok", false},
            {"error", "request must be an object with string field 'method'"},
        };
    }

    auto call    = std::make_shared<PendingCall>();
    call->id     = request.value("id", nlohmann::json(nullptr));
    call->method = request["method"].get<std::string>();
    if (request.contains("params")) {
        call->params = request["params"];
    }

    if (!enqueueCall(call)) {
        return {
            {"id", call->id},
            {"ok", false},
            {"error", "automation control server is not accepting requests"},
        };
    }

    std::unique_lock lock(call->mutex);
    call->cv.wait(lock, [&]()
                  { return call->bCompleted || _bStopRequested.load(); });
    if (!call->bCompleted) {
        return {
            {"id", call->id},
            {"ok", false},
            {"error", "automation control request interrupted"},
        };
    }
    return call->response;
}

void AppAutomationControlService::update(App& app)
{
    std::deque<std::shared_ptr<PendingCall>> incoming;
    {
        std::scoped_lock lock(_incomingMutex);
        incoming.swap(_incomingCalls);
    }

    for (auto& call : incoming) {
        handleCall(app, call);
    }
}

void AppAutomationControlService::onFrameCompleted(App&                         app,
                                                   IRender*                     render,
                                                   std::shared_ptr<RenderImage> postprocessImage,
                                                   std::shared_ptr<RenderImage> viewportImage,
                                                   std::shared_ptr<RenderImage> presentationImage,
                                                   uint64_t                     frameIndex)
{
    if (!_pendingScreenshot) {
        return;
    }

    auto& screenshot = *_pendingScreenshot;
    AppScreenshotCapture::tryFinalize(frameIndex, screenshot.state);
    if (screenshot.state.bCompleted) {
        completeCall(screenshot.waiter,
                     makeSuccess(*screenshot.waiter,
                                 {
                                     {"path", std::filesystem::path(screenshot.outputPath).generic_string()},
                                     {"target", screenshot.target == EAutomationScreenshotTarget::Presentation ? "presentation" : "viewport"},
                                 }));
        _pendingScreenshot.reset();
        return;
    }
    if (screenshot.state.bFailed) {
        completeCall(screenshot.waiter,
                     makeError(*screenshot.waiter, "screenshot capture failed"));
        _pendingScreenshot.reset();
        return;
    }

    if (screenshot.state.pendingJob || screenshot.state.bPendingPresentationCapture || screenshot.state.bPresentationCopyRecorded) {
        return;
    }

    if (frameIndex < screenshot.earliestFrameIndex) {
        return;
    }

    if (!AppScreenshotCapture::request(render,
                                       AppAutomation::buildOffscreenJobQueueService(app),
                                       std::move(postprocessImage),
                                       std::move(viewportImage),
                                       std::move(presentationImage),
                                       screenshot.state,
                                       screenshot.outputPath,
                                       screenshot.target)) {
        completeCall(screenshot.waiter,
                     makeError(*screenshot.waiter, "failed to enqueue screenshot request"));
        _pendingScreenshot.reset();
    }
}

bool AppAutomationControlService::appendPresentationCapture(uint64_t frameIndex,
                                                            RenderGraph&    graph,
                                                            RGTextureHandle presentationOutput,
                                                            Extent2D        presentationExtent)
{
    if (!_pendingScreenshot) {
        return false;
    }

    return AppScreenshotCapture::appendPresentationCapture(
        frameIndex,
        _pendingScreenshot->state,
        graph,
        presentationOutput,
        presentationExtent);
}

void AppAutomationControlService::handleCall(App& app, const std::shared_ptr<PendingCall>& call)
{
    if (call->method == "ping") {
        handlePing(call);
        return;
    }
    if (call->method == "get_point_light_pos") {
        handleGetPointLightPos(app, call);
        return;
    }
    if (call->method == "get_directional_light_info") {
        handleGetDirectionalLightInfo(app, call);
        return;
    }
    if (call->method == "set_render_pipeline") {
        handleSetRenderPipeline(app, call);
        return;
    }
    if (call->method == "set_shadow_settings") {
        handleSetShadowSettings(app, call);
        return;
    }
    if (call->method == "set_app_state") {
        handleSetAppState(app, call);
        return;
    }
    if (call->method == "set_editor_camera") {
        handleSetEditorCamera(app, call);
        return;
    }
    if (call->method == "capture_screenshot") {
        handleCaptureScreenshot(app, call);
        return;
    }
    if (call->method == "quit") {
        handleQuit(app, call);
        return;
    }
    if (call->method == "get_world_view_state") {
        handleGetWorldViewState(app, call);
        return;
    }
    if (call->method == "list_overlay_sprites") {
        handleListOverlaySprites(app, call);
        return;
    }
    if (call->method == "list_billboard_components") {
        handleListBillboardComponents(app, call);
        return;
    }
    if (call->method == "list_scene_entities") {
        handleListSceneEntities(app, call);
        return;
    }
    if (call->method == "get_entity_info") {
        handleGetEntityInfo(app, call);
        return;
    }
    if (call->method == "find_entities_near") {
        handleFindEntitiesNear(app, call);
        return;
    }
    if (call->method == "create_billboard_regression_scene") {
        handleCreateBillboardRegressionScene(app, call);
        return;
    }
    if (call->method == "set_editor_config_value") {
        handleSetEditorConfigValue(app, call);
        return;
    }
    if (call->method == "entity_remove_component") {
        handleEntityRemoveComponent(app, call);
        return;
    }
    if (call->method == "entity_set_mesh_visible") {
        handleEntitySetMeshVisible(app, call);
        return;
    }
    if (call->method == "eval_js") {
        handleEvalJS(app, call);
        return;
    }
    if (call->method == "invoke") {
        handleInvoke(app, call);
        return;
    }
    if (call->method == "list_commands") {
        handleListCommands(app, call);
        return;
    }

    completeCall(call, makeError(*call, std::string("unknown method: ") + call->method));
}

void AppAutomationControlService::handleEvalJS(App& app, const std::shared_ptr<PendingCall>& call)
{
    auto* scripting = app.getJSScriptingSystem();
    if (scripting == nullptr) {
        completeCall(call, makeError(*call, "js scripting unavailable"));
        return;
    }

    const std::string source = call->params.value("source", "");
    auto              result = scripting->evalJS(source, "<rpc>");
    if (!result.ok) {
        completeCall(call, makeError(*call, result.error));
        return;
    }
    completeCall(call, makeSuccess(*call, {{"result", std::move(result.value)}}));
}

void AppAutomationControlService::handleInvoke(App& app, const std::shared_ptr<PendingCall>& call)
{
    auto* scripting = app.getJSScriptingSystem();
    if (scripting == nullptr) {
        completeCall(call, makeError(*call, "js scripting unavailable"));
        return;
    }

    const std::string name = call->params.value("name", "");
    nlohmann::json args = call->params.contains("args") ? call->params["args"] : nlohmann::json::object();

    nlohmann::json result;
    std::string    error;
    if (!scripting->invoke(name, args, result, error)) {
        completeCall(call, makeError(*call, error));
        return;
    }
    completeCall(call, makeSuccess(*call, {{"result", std::move(result)}}));
}

void AppAutomationControlService::handleListCommands(App& app, const std::shared_ptr<PendingCall>& call)
{
    auto* scripting = app.getJSScriptingSystem();
    if (scripting == nullptr) {
        completeCall(call, makeError(*call, "js scripting unavailable"));
        return;
    }
    completeCall(call, makeSuccess(*call, {{"commands", scripting->buildCommandList()}}));
}

void AppAutomationControlService::handlePing(const std::shared_ptr<PendingCall>& call)
{
    completeCall(call,
                 makeSuccess(*call,
                             {
                                 {"service", "automation-control"},
                                 {"port", _port},
                             }));
}

void AppAutomationControlService::handleGetPointLightPos(App& app, const std::shared_ptr<PendingCall>& call)
{
    Scene* scene = resolveControlScene(app);
    if (!scene) {
        completeCall(call, makeError(*call, "no active scene available"));
        return;
    }

    const auto position = findFirstPointLightPosition(scene);
    if (!position) {
        completeCall(call, makeError(*call, "no point light found"));
        return;
    }

    completeCall(call,
                 makeSuccess(*call,
                             {
                                 {"position", {position->x, position->y, position->z}},
                             }));
}

void AppAutomationControlService::handleGetDirectionalLightInfo(App& app, const std::shared_ptr<PendingCall>& call)
{
    Scene* scene = resolveControlScene(app);
    if (!scene) {
        completeCall(call, makeError(*call, "no active scene available"));
        return;
    }

    const auto info = findFirstDirectionalLightInfo(scene);
    if (!info) {
        completeCall(call, makeError(*call, "no directional light found"));
        return;
    }

    nlohmann::json result = {
        {"direction", {info->direction.x, info->direction.y, info->direction.z}},
        {"color", {info->color.x, info->color.y, info->color.z}},
        {"intensity", info->intensity},
    };
    if (info->position) {
        result["position"] = {info->position->x, info->position->y, info->position->z};
    }
    else {
        result["position"] = nullptr;
    }

    completeCall(call, makeSuccess(*call, std::move(result)));
}

void AppAutomationControlService::handleSetRenderPipeline(App& app, const std::shared_ptr<PendingCall>& call)
{
    auto* renderRuntime = app.getRenderServices().getRenderRuntime();
    if (!renderRuntime) {
        completeCall(call, makeError(*call, "render runtime is unavailable"));
        return;
    }

    const auto targetText = call->params.value("target", std::string{});
    EAutomationRenderPipeline target{};
    if (!tryParseAutomationRenderPipeline(targetText, target)) {
        completeCall(call, makeError(*call, "set_render_pipeline requires target 'forward' or 'deferred'"));
        return;
    }

    const auto runtimeTarget = target == EAutomationRenderPipeline::Forward
                                  ? RenderRuntime::ERenderPipeline::Forward
                                  : RenderRuntime::ERenderPipeline::Deferred;
    renderRuntime->setPendingRenderPipeline(runtimeTarget);
    completeCall(call,
                 makeSuccess(*call,
                             {
                                 {"target", target == EAutomationRenderPipeline::Forward ? "forward" : "deferred"},
                                 {"applied", renderRuntime->getRenderPipeline() == runtimeTarget},
                             }));
}

void AppAutomationControlService::handleSetShadowSettings(App& app, const std::shared_ptr<PendingCall>& call)
{
    auto& settings = app.getRenderServices().getShadowSettings();
    const auto setBool = [&](const char* key, bool& target) -> bool {
        if (!call->params.contains(key)) {
            return true;
        }
        if (!call->params[key].is_boolean()) {
            completeCall(call, makeError(*call, std::string("set_shadow_settings requires boolean '") + key + "'"));
            return false;
        }
        target = call->params[key].get<bool>();
        return true;
    };

    if (!setBool("pointLightEnabled", settings.pointLightEnabled) ||
        !setBool("pointLightUseIndirect", settings.pointLightUseIndirect) ||
        !setBool("pointLightIndirectCullEnabled", settings.pointLightIndirectCullEnabled) ||
        !setBool("directionalEnabled", settings.directionalEnabled)) {
        return;
    }

    completeCall(call,
                 makeSuccess(*call,
                             {
                                 {"pointLightEnabled", settings.pointLightEnabled},
                                 {"pointLightUseIndirect", settings.pointLightUseIndirect},
                                 {"pointLightIndirectCullEnabled", settings.pointLightIndirectCullEnabled},
                                 {"directionalEnabled", settings.directionalEnabled},
                             }));
}

void AppAutomationControlService::handleSetAppState(App& app, const std::shared_ptr<PendingCall>& call)
{
    const std::string state = call->params.value("state", std::string{});
    if (state != "runtime" && state != "simulation" && state != "stopped") {
        completeCall(call, makeError(*call, "set_app_state requires state 'runtime', 'simulation' or 'stopped'"));
        return;
    }

    app.getTaskManager().registerFrameTask([&app, state]() {
        if (state == "runtime" && app.isStopped()) {
            app.startRuntime();
        }
        else if (state == "simulation" && app.isStopped()) {
            app.startSimulation();
        }
        else if (state == "stopped") {
            if (app.isRuntimeMode()) {
                app.stopRuntime();
            }
            else if (app.isSimulationMode()) {
                app.stopSimulation();
            }
        }
    });

    completeCall(call, makeSuccess(*call, {{"state", state}}));
}

void AppAutomationControlService::handleSetEditorCamera(App& app, const std::shared_ptr<PendingCall>& call)
{
    auto* editorControl = getEditorAutomationControl(app);
    if (!editorControl) {
        completeCall(call, makeError(*call, "editor automation control is unavailable"));
        return;
    }

    if (call->params.contains("look_at") && !call->params["look_at"].is_null()) {
        const auto target = parseVec3Param(call->params["look_at"]);
        if (!target) {
            completeCall(call, makeError(*call, "set_editor_camera params.look_at must be vec3"));
            return;
        }

        const float distance     = call->params.value("distance", 4.0f);
        const float heightOffset = call->params.value("height_offset", 1.0f);
        if (!editorControl->focusEditorCameraOnWorldPoint(*target, distance, heightOffset)) {
            completeCall(call, makeError(*call, "failed to focus editor camera"));
            return;
        }

        completeCall(call, makeSuccess(*call));
        return;
    }

    const auto position = parseVec3Param(call->params.value("position", nlohmann::json()));
    const auto rotation = parseVec3Param(call->params.value("rotation", nlohmann::json()));
    if (!position || !rotation) {
        completeCall(call, makeError(*call, "set_editor_camera requires params.position and params.rotation vec3, or params.look_at vec3"));
        return;
    }

    if (!editorControl->setEditorCameraTransform(*position, *rotation)) {
        completeCall(call, makeError(*call, "failed to update editor camera"));
        return;
    }

    completeCall(call, makeSuccess(*call));
}

void AppAutomationControlService::handleCaptureScreenshot(App& app, const std::shared_ptr<PendingCall>& call)
{
    if (_pendingScreenshot) {
        completeCall(call, makeError(*call, "a screenshot request is already in flight"));
        return;
    }

    const std::string outputPath = call->params.value("path", std::string{});
    if (outputPath.empty()) {
        completeCall(call, makeError(*call, "capture_screenshot requires params.path"));
        return;
    }

    const auto target = parseScreenshotTarget(call->params);
    if (!target) {
        completeCall(call, makeError(*call, "capture_screenshot params.target must be 'viewport' or 'presentation'"));
        return;
    }

    const uint64_t warmupFrames = call->params.value("warmup_frames", static_cast<uint64_t>(60));
    _pendingScreenshot          = ScreenshotRequest{
        .waiter             = call,
        .outputPath         = outputPath,
        .target             = *target,
        .earliestFrameIndex = App::currentFrameIndex() + warmupFrames,
    };
}

void AppAutomationControlService::handleQuit(App& app, const std::shared_ptr<PendingCall>& call)
{
    app.requestQuit();
    completeCall(call, makeSuccess(*call));
}

void AppAutomationControlService::handleGetWorldViewState(App& app, const std::shared_ptr<PendingCall>& call)
{
    auto* renderRuntime = app.getRenderServices().getRenderRuntime();
    if (!renderRuntime) {
        completeCall(call, makeError(*call, "render runtime is unavailable"));
        return;
    }

    const auto& frameState = app.getRenderServices().getRenderFrameState();
    const auto  viewportRect = renderRuntime->getViewportRect();

    nlohmann::json result = {
        {"is_stopped", app.isStopped()},
        {"is_runtime", app.isRuntimeMode()},
        {"is_simulation", app.isSimulationMode()},
        {"is_paused", app.isPaused()},
        {"camera_pos", {frameState.cameraPos.x, frameState.cameraPos.y, frameState.cameraPos.z}},
        {"viewport_rect", {
            {"x", viewportRect.pos.x},
            {"y", viewportRect.pos.y},
            {"width", viewportRect.extent.x},
            {"height", viewportRect.extent.y},
        }},
        {"frame_index", App::currentFrameIndex()},
    };

    completeCall(call, makeSuccess(*call, std::move(result)));
}

void AppAutomationControlService::handleListOverlaySprites(App& app, const std::shared_ptr<PendingCall>& call)
{
    Scene* scene = resolveControlScene(app);
    if (!scene) {
        completeCall(call, makeError(*call, "no active scene available"));
        return;
    }

    nlohmann::json spritesArray = nlohmann::json::array();
    for (const auto& [entity, billboard, transform] : scene->getRegistry().view<BillboardComponent, TransformComponent>().each()) {
        (void)entity;

        nlohmann::json entry = {
            {"world_center", {transform.getWorldPosition().x, transform.getWorldPosition().y, transform.getWorldPosition().z}},
            {"world_direction", {billboard.worldDirection.x, billboard.worldDirection.y, billboard.worldDirection.z}},
            {"screen_size_pixels", billboard.screenSizePixels},
            {"min_world_scale", billboard.minWorldScale},
            {"managed_by_light", billboard.bManagedByLight},
            {"owner_entity_id", static_cast<uint32_t>(entity)},
        };
        spritesArray.push_back(std::move(entry));
    }

    completeCall(call,
                 makeSuccess(*call,
                             {
                                 {"count", spritesArray.size()},
                                 {"sprites", std::move(spritesArray)},
                             }));
}

void AppAutomationControlService::handleListBillboardComponents(App& app, const std::shared_ptr<PendingCall>& call)
{
    Scene* scene = resolveControlScene(app);
    if (!scene) {
        completeCall(call, makeError(*call, "no active scene available"));
        return;
    }

    nlohmann::json billboards = nlohmann::json::array();
    for (const auto& [entity, billboard] : scene->getRegistry().view<BillboardComponent>().each()) {
        nlohmann::json entry = {
            {"entity_id", static_cast<uint32_t>(entity)},
            {"visible", billboard.bVisible},
            {"managed_by_light", billboard.bManagedByLight},
            {"owner_entity_id", static_cast<uint32_t>(entity)},
            {"screen_size_pixels", billboard.screenSizePixels},
            {"min_world_scale", billboard.minWorldScale},
            {"world_direction", {billboard.worldDirection.x, billboard.worldDirection.y, billboard.worldDirection.z}},
            {"texture_path", billboard.image.textureRef.getPath()},
        };
        billboards.push_back(std::move(entry));
    }

    completeCall(call,
                 makeSuccess(*call,
                             {
                                 {"count", billboards.size()},
                                 {"billboards", std::move(billboards)},
                             }));
}

void AppAutomationControlService::handleListSceneEntities(App& app, const std::shared_ptr<PendingCall>& call)
{
    Scene* scene = resolveControlScene(app);
    if (!scene) {
        completeCall(call, makeError(*call, "no active scene available"));
        return;
    }

    nlohmann::json entities = nlohmann::json::array();
    for (const auto& [handle, entity] : scene->_entityMap) {
        entities.push_back({
            {"id", static_cast<uint32_t>(handle)},
            {"name", entity.name},
            {"components", enumerateComponentDetails(scene->_registry, handle)},
        });
    }

    completeCall(call,
                 makeSuccess(*call,
                             {
                                 {"count", entities.size()},
                                 {"entities", std::move(entities)},
                             }));
}

void AppAutomationControlService::handleGetEntityInfo(App& app, const std::shared_ptr<PendingCall>& call)
{
    Scene* scene = resolveControlScene(app);
    if (!scene) {
        completeCall(call, makeError(*call, "no active scene available"));
        return;
    }

    const uint32_t entityId = call->params.value("id", 0u);
    if (entityId == 0 && !call->params.contains("name")) {
        completeCall(call, makeError(*call, "get_entity_info requires params.id (uint) or params.name (string)"));
        return;
    }

    // Find entity by id or name
    const Entity* target = nullptr;
    if (entityId != 0) {
        auto it = scene->_entityMap.find(entt::entity{entityId});
        if (it != scene->_entityMap.end()) {
            target = &it->second;
        }
    }
    else if (call->params.contains("name")) {
        const std::string name = call->params["name"].get<std::string>();
        for (const auto& [handle, entity] : scene->_entityMap) {
            if (entity.name == name) {
                target = &entity;
                break;
            }
        }
    }

    if (!target) {
        completeCall(call, makeError(*call, "entity not found"));
        return;
    }

    nlohmann::json result = {
        {"id", target->getId()},
        {"name", target->name},
        {"components", enumerateComponentDetails(scene->_registry, target->getHandle())},
    };

    // Detail fields for commonly-queried component types
    if (auto* transform = scene->_registry.try_get<TransformComponent>(target->getHandle())) {
        const glm::vec3 pos = transform->getWorldPosition();
        result["transform"] = {
            {"position", {pos.x, pos.y, pos.z}},
        };
    }

    // If has PointLightComponent, include light details
    if (auto* light = scene->_registry.try_get<PointLightComponent>(target->getHandle())) {
        result["point_light"] = {
            {"color", {light->color.x, light->color.y, light->color.z}},
            {"intensity", light->intensity},
        };
    }

    // If has DirectionalLightComponent, include light details
    if (auto* dlight = scene->_registry.try_get<DirectionalLightComponent>(target->getHandle())) {
        result["directional_light"] = {
            {"color", {dlight->_color.x, dlight->_color.y, dlight->_color.z}},
            {"intensity", dlight->intensity},
            {"enabled", dlight->bEnable},
        };
    }

    // If has ModelComponent, include model path
    if (auto* modelComp = scene->_registry.try_get<ModelComponent>(target->getHandle())) {
        if (modelComp->hasModelSource()) {
            result["model"] = {
                {"path", modelComp->_modelRef.getPath()},
                {"mesh_count", modelComp->getMeshCount()},
            };
        }
    }

    // If has RenderComponent, include layer info
    if (auto* renderComp = scene->_registry.try_get<RenderComponent>(target->getHandle())) {
        result["render"] = {
            {"layer", static_cast<int>(renderComp->RenderingLayer)},
        };
    }

    completeCall(call, makeSuccess(*call, std::move(result)));
}

void AppAutomationControlService::handleFindEntitiesNear(App& app, const std::shared_ptr<PendingCall>& call)
{
    Scene* scene = resolveControlScene(app);
    if (!scene) {
        completeCall(call, makeError(*call, "no active scene available"));
        return;
    }

    const auto pos = parseVec3Param(call->params.value("position", nlohmann::json()));
    if (!pos) {
        completeCall(call, makeError(*call, "find_entities_near requires params.position as vec3"));
        return;
    }

    const float radius = call->params.value("radius", 5.0f);
    const float radiusSq = radius * radius;

    nlohmann::json entities = nlohmann::json::array();
    for (const auto& [handle, entity] : scene->_entityMap) {
        auto* transform = scene->_registry.try_get<TransformComponent>(handle);
        if (!transform) {
            continue;
        }

        const glm::vec3 entityPos = transform->getWorldPosition();
        const glm::vec3 delta = entityPos - *pos;
        const float distSq = glm::length2(delta);
        if (distSq > radiusSq) {
            continue;
        }

        entities.push_back({
            {"id", static_cast<uint32_t>(handle)},
            {"name", entity.name},
            {"position", {entityPos.x, entityPos.y, entityPos.z}},
            {"distance", std::sqrt(distSq)},
            {"components", enumerateComponentDetails(scene->_registry, handle)},
        });
    }

    completeCall(call,
                 makeSuccess(*call,
                             {
                                 {"center", {pos->x, pos->y, pos->z}},
                                 {"radius", radius},
                                 {"count", entities.size()},
                                 {"entities", std::move(entities)},
                             }));
}

void AppAutomationControlService::handleCreateBillboardRegressionScene(App& app, const std::shared_ptr<PendingCall>& call)
{
    Scene* scene = createBillboardRegressionScene(app);
    if (!scene) {
        completeCall(call, makeError(*call, "failed to create billboard regression scene"));
        return;
    }

    completeCall(call,
                 makeSuccess(*call,
                             {
                                 {"scene_name", scene->getName()},
                                 {"entity_count", scene->_entityMap.size()},
                             }));
}

void AppAutomationControlService::handleSetEditorConfigValue(App& app, const std::shared_ptr<PendingCall>& call)
{
    (void)app;

    const std::string key = call->params.value("key", std::string{});
    if (key.empty()) {
        completeCall(call, makeError(*call, "set_editor_config_value requires params.key"));
        return;
    }
    if (!call->params.contains("value")) {
        completeCall(call, makeError(*call, "set_editor_config_value requires params.value"));
        return;
    }

    auto editor = ConfigManager::Editor("editor");
    editor.setFlushOnDestroy(false);
    editor.set(key, call->params["value"]);
    const bool flushed = editor.flush();

    completeCall(call,
                 makeSuccess(*call,
                             {
                                 {"key", key},
                                 {"flushed", flushed},
                             }));
}

void AppAutomationControlService::handleEntityRemoveComponent(App& app, const std::shared_ptr<PendingCall>& call)
{
    Scene* scene = resolveControlScene(app);
    if (!scene) {
        completeCall(call, makeError(*call, "no active scene available"));
        return;
    }

    const uint32_t entityId = call->params.value("id", 0u);
    if (entityId == 0) {
        completeCall(call, makeError(*call, "entity_remove_component requires params.id (uint)"));
        return;
    }

    if (!call->params.contains("component") || !call->params["component"].is_string()) {
        completeCall(call, makeError(*call, "entity_remove_component requires params.component (string)"));
        return;
    }

    entt::entity handle{entityId};
    if (!scene->_registry.valid(handle)) {
        completeCall(call, makeError(*call, "entity not valid"));
        return;
    }

    const FName componentName(call->params["component"].get<std::string>());
    const bool ok = ECSRegistry::get().removeComponent(componentName, scene->getRegistry(), handle);

    completeCall(call,
                 makeSuccess(*call,
                             {
                                 {"removed", ok},
                                 {"component", componentName.c_str()},
                                 {"entity_id", entityId},
                             }));
}

void AppAutomationControlService::handleEntitySetMeshVisible(App& app, const std::shared_ptr<PendingCall>& call)
{
    Scene* scene = resolveControlScene(app);
    if (!scene) {
        completeCall(call, makeError(*call, "no active scene available"));
        return;
    }

    const uint32_t entityId = call->params.value("id", 0u);
    if (entityId == 0) {
        completeCall(call, makeError(*call, "entity_set_mesh_visible requires params.id (uint)"));
        return;
    }

    entt::entity handle{entityId};
    if (!scene->_registry.valid(handle)) {
        completeCall(call, makeError(*call, "entity not valid"));
        return;
    }

    auto* meshComp = scene->_registry.try_get<StaticMeshComponent>(handle);
    if (!meshComp) {
        completeCall(call, makeError(*call, "entity does not have StaticMeshComponent"));
        return;
    }

    const bool visible = call->params.value("visible", true);
    meshComp->_mesh.setPrimitiveGeometry(visible ? EPrimitiveGeometry::Cube : EPrimitiveGeometry::None);
    meshComp->_mesh.invalidate();

    completeCall(call,
                 makeSuccess(*call,
                             {
                                 {"entity_id", entityId},
                                 {"visible", visible},
                                 {"primitive", visible ? "Cube" : "None"},
                             }));
}

void AppAutomationControlService::completeCall(const std::shared_ptr<PendingCall>& call, nlohmann::json response)
{
    {
        std::scoped_lock lock(call->mutex);
        call->response   = std::move(response);
        call->bCompleted = true;
    }
    call->cv.notify_one();
}

nlohmann::json AppAutomationControlService::makeSuccess(const PendingCall& call, nlohmann::json result) const
{
    return {
        {"id", call.id},
        {"ok", true},
        {"result", std::move(result)},
    };
}

nlohmann::json AppAutomationControlService::makeError(const PendingCall& call, std::string_view message) const
{
    return {
        {"id", call.id},
        {"ok", false},
        {"error", std::string(message)},
    };
}

} // namespace ya
