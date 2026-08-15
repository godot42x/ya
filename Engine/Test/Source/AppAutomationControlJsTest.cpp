#include "AppModuleTestAccess.h"

#include "Core/Scripting/ScriptApiAsset.h"
#include "Core/Scripting/ScriptApiRegistry.h"
#include "Scene3D/TransformComponent.h"
#include "ECS/Entity.h"
#include "ECS/Systems/JSScriptingSystem.h"
#include "GameRuntime/App.h"
#include "GameRuntime/Automation/AppAutomationControlService.h"
#include "Scene/Core/Scene.h"
#include "Scene/Runtime/SceneManager.h"

#include <gtest/gtest.h>

#include <asio.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

namespace ya
{

namespace
{

/// Picks a currently-free TCP port by binding port 0 and releasing it.
uint16_t pickFreePort()
{
    asio::io_context        io;
    asio::ip::tcp::acceptor acceptor(io);
    asio::error_code        ec;

    acceptor.open(asio::ip::tcp::v4(), ec);
    EXPECT_FALSE(ec);
    acceptor.bind(asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), 0), ec);
    EXPECT_FALSE(ec);

    const uint16_t port = acceptor.local_endpoint(ec).port();
    EXPECT_FALSE(ec);
    return port;
}

/// Minimal line-oriented JSON-RPC client over 127.0.0.1.
class AutomationRpcClient
{
  public:
    ~AutomationRpcClient() { close(); }

    bool connectWithRetry(uint16_t port, int attempts = 100)
    {
        for (int i = 0; i < attempts; ++i) {
            if (connectOnce(port)) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return false;
    }

    bool sendLine(const std::string& line)
    {
        const std::string payload = line + "\n";
        size_t            offset  = 0;
        asio::error_code  ec;
        while (offset < payload.size()) {
            const size_t n = _socket.write_some(asio::buffer(payload.data() + offset, payload.size() - offset), ec);
            if (ec == asio::error::would_block || ec == asio::error::try_again) {
                ec.clear();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            if (ec || n == 0) {
                return false;
            }
            offset += n;
        }
        return true;
    }

    /// Returns one '\n'-terminated line when it arrives within timeoutMs;
    /// partial data stays buffered across calls.
    bool tryReadLine(std::string& out, int timeoutMs)
    {
        asio::error_code ec;
        _socket.non_blocking(true, ec);
        if (ec) {
            return false;
        }

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        for (;;) {
            char chunk[4096];
            const size_t n = _socket.read_some(asio::buffer(chunk), ec);
            if (!ec && n > 0) {
                _buffer.append(chunk, n);

                const size_t newline = _buffer.find('\n');
                if (newline == std::string::npos) {
                    return false;
                }
                out = _buffer.substr(0, newline);
                if (!out.empty() && out.back() == '\r') {
                    out.pop_back();
                }
                _buffer.erase(0, newline + 1);
                return true;
            }
            if (ec == asio::error::would_block || ec == asio::error::try_again) {
                ec.clear();
                if (std::chrono::steady_clock::now() >= deadline) {
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            return false;
        }
    }

    void close()
    {
        if (_socket.is_open()) {
            asio::error_code ec;
            _socket.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
            _socket.close(ec);
        }
    }

  private:
    bool connectOnce(uint16_t port)
    {
        asio::error_code ec;
        if (!_socket.is_open()) {
            _socket.open(asio::ip::tcp::v4(), ec);
            if (ec) {
                return false;
            }
        }

        const auto endpoint = asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), port);
        _socket.connect(endpoint, ec);
        if (!ec) {
            return true;
        }
        _socket.close(ec);
        return false;
    }

    asio::io_context      _io;
    asio::ip::tcp::socket _socket{_io};
    std::string           _buffer;
};

/// Sends one request and pumps the automation service until the listener
/// thread answers. Mirrors AppFrameLoop's per-frame `update()` call.
nlohmann::json rpcRaw(App& app, AutomationRpcClient& client, const std::string& rawLine)
{
    EXPECT_TRUE(client.sendLine(rawLine)) << rawLine;

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    std::string line;
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto* service = app.getAutomationControlService()) {
            service->update(app);
        }
        if (client.tryReadLine(line, 5)) {
            return nlohmann::json::parse(line);
        }
    }

    ADD_FAILURE() << "timed out waiting for automation rpc response";
    return nullptr;
}

nlohmann::json rpc(App& app, AutomationRpcClient& client, const nlohmann::json& request)
{
    return rpcRaw(app, client, request.dump());
}

} // namespace

class AppAutomationControlJsTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        registerCoreScriptApis(ScriptApiRegistry::get());
        registerAssetScriptApis(ScriptApiRegistry::get());

        _sceneManager = std::make_shared<SceneManager>();
        AppModuleTestAccess::setSceneManager(_app, _sceneManager.get());
        _scene = std::make_shared<Scene>("AutomationJs");
        ASSERT_TRUE(_sceneManager->activateScene(_scene));

        auto& api = ScriptApiRegistry::get();
        api.setActiveSceneProvider([this]() -> Scene* { return _sceneManager->getActiveScene(); });
        api.setSaveSceneFn([this](const std::string& path, Scene& scene) -> bool
                           { return _sceneManager->serializeToFile(path, &scene); });
        api.setLoadSceneFn([this](const std::string& path) -> bool { return _sceneManager->loadScene(path); });

        _js.init();
        AppModuleTestAccess::setJSScriptingSystem(_app, &_js);

        _port = pickFreePort();
        ASSERT_TRUE(_app.getAutomationControlService()->init(_port));
    }

    void TearDown() override
    {
        _app.getAutomationControlService()->shutdown();
        AppModuleTestAccess::setJSScriptingSystem(_app, nullptr);
        _js.shutdown();
        AppModuleTestAccess::setSceneManager(_app, nullptr);
    }

    App                           _app;
    std::shared_ptr<SceneManager> _sceneManager;
    std::shared_ptr<Scene>        _scene;
    JSScriptingSystem             _js;
    uint16_t                      _port = 0;
};

// ============================================================================
// Wire protocol
// ============================================================================

TEST_F(AppAutomationControlJsTest, PingRoundTrip)
{
    AutomationRpcClient client;
    ASSERT_TRUE(client.connectWithRetry(_port));

    const auto response = rpc(_app, client, {{"method", "ping"}, {"id", 1}});
    ASSERT_TRUE(response.contains("ok")) << response.dump();
    EXPECT_TRUE(response["ok"].get<bool>());
    EXPECT_EQ(response["id"], 1);
    EXPECT_EQ(response["result"]["service"], "automation-control");
    EXPECT_EQ(response["result"]["port"], _port);
}

TEST_F(AppAutomationControlJsTest, InvalidJsonLineReturnsError)
{
    AutomationRpcClient client;
    ASSERT_TRUE(client.connectWithRetry(_port));

    const auto response = rpcRaw(_app, client, "this is not json");
    ASSERT_TRUE(response.contains("ok"));
    EXPECT_FALSE(response["ok"].get<bool>());
    EXPECT_TRUE(response["id"].is_null());
    EXPECT_NE(response["error"].get<std::string>().find("invalid json"), std::string::npos);
}

TEST_F(AppAutomationControlJsTest, RequestWithoutMethodReturnsError)
{
    AutomationRpcClient client;
    ASSERT_TRUE(client.connectWithRetry(_port));

    const auto response = rpc(_app, client, {{"id", 7}, {"params", nlohmann::json::object()}});
    ASSERT_TRUE(response.contains("ok"));
    EXPECT_FALSE(response["ok"].get<bool>());
    EXPECT_EQ(response["id"], 7);
    EXPECT_NE(response["error"].get<std::string>().find("method"), std::string::npos);
}

TEST_F(AppAutomationControlJsTest, UnknownMethodReturnsError)
{
    AutomationRpcClient client;
    ASSERT_TRUE(client.connectWithRetry(_port));

    const auto response = rpc(_app, client, {{"method", "no_such_method"}, {"id", 8}});
    ASSERT_TRUE(response.contains("ok"));
    EXPECT_FALSE(response["ok"].get<bool>());
    EXPECT_NE(response["error"].get<std::string>().find("unknown method"), std::string::npos);
}

// ============================================================================
// eval_js
// ============================================================================

TEST_F(AppAutomationControlJsTest, EvalJsReturnsLastExpressionValue)
{
    AutomationRpcClient client;
    ASSERT_TRUE(client.connectWithRetry(_port));

    const auto response = rpc(_app, client,
                              {{"method", "eval_js"},
                               {"id", 10},
                               {"params", {{"source", "1 + 2 * 3"}}}});
    ASSERT_TRUE(response["ok"].get<bool>()) << response.dump();
    EXPECT_EQ(response["result"]["result"], 7);
}

TEST_F(AppAutomationControlJsTest, EvalJsRoundTripsAllJsonTypes)
{
    AutomationRpcClient client;
    ASSERT_TRUE(client.connectWithRetry(_port));

    const auto response = rpc(_app, client,
                              {{"method", "eval_js"},
                               {"id", 11},
                               {"params",
                                {{"source",
                                  R"(({ n: 1.5, i: 42, s: "hello", b: true, a: [1, "two", false], o: { k: "v" }, z: null }))"}}}});
    ASSERT_TRUE(response["ok"].get<bool>()) << response.dump();

    const auto& result = response["result"]["result"];
    EXPECT_EQ(result["n"], 1.5);
    EXPECT_EQ(result["i"], 42);
    EXPECT_EQ(result["s"], "hello");
    EXPECT_EQ(result["b"], true);
    ASSERT_TRUE(result["a"].is_array());
    EXPECT_EQ(result["a"][0], 1);
    EXPECT_EQ(result["a"][1], "two");
    EXPECT_EQ(result["a"][2], false);
    EXPECT_EQ(result["o"]["k"], "v");
    EXPECT_TRUE(result["z"].is_null());
}

TEST_F(AppAutomationControlJsTest, EvalJsUndefinedBecomesNull)
{
    AutomationRpcClient client;
    ASSERT_TRUE(client.connectWithRetry(_port));

    const auto response = rpc(_app, client,
                              {{"method", "eval_js"},
                               {"id", 12},
                               {"params", {{"source", "let x = 5;"}}}});
    ASSERT_TRUE(response["ok"].get<bool>()) << response.dump();
    EXPECT_TRUE(response["result"]["result"].is_null());
}

TEST_F(AppAutomationControlJsTest, EvalJsMissingSourceTreatsAsEmptyScript)
{
    AutomationRpcClient client;
    ASSERT_TRUE(client.connectWithRetry(_port));

    const auto response = rpc(_app, client, {{"method", "eval_js"}, {"id", 13}});
    ASSERT_TRUE(response["ok"].get<bool>()) << response.dump();
    EXPECT_TRUE(response["result"]["result"].is_null());
}

TEST_F(AppAutomationControlJsTest, EvalJsSyntaxErrorPropagates)
{
    AutomationRpcClient client;
    ASSERT_TRUE(client.connectWithRetry(_port));

    const auto response = rpc(_app, client,
                              {{"method", "eval_js"},
                               {"id", 14},
                               {"params", {{"source", "this is not (valid js"}}}});
    ASSERT_TRUE(response.contains("ok"));
    EXPECT_FALSE(response["ok"].get<bool>());
    EXPECT_FALSE(response["error"].get<std::string>().empty());
}

TEST_F(AppAutomationControlJsTest, EvalJsRuntimeErrorPropagates)
{
    AutomationRpcClient client;
    ASSERT_TRUE(client.connectWithRetry(_port));

    const auto response = rpc(_app, client,
                              {{"method", "eval_js"},
                               {"id", 15},
                               {"params", {{"source", "null.foo"}}}});
    ASSERT_TRUE(response.contains("ok"));
    EXPECT_FALSE(response["ok"].get<bool>());
    EXPECT_FALSE(response["error"].get<std::string>().empty());
}

TEST_F(AppAutomationControlJsTest, EvalJsStatePersistsAcrossCalls)
{
    AutomationRpcClient client;
    ASSERT_TRUE(client.connectWithRetry(_port));

    const auto first = rpc(_app, client,
                           {{"method", "eval_js"},
                            {"id", 16},
                            {"params", {{"source", "globalThis.counter = (globalThis.counter || 0) + 1"}}}});
    ASSERT_TRUE(first["ok"].get<bool>()) << first.dump();
    EXPECT_EQ(first["result"]["result"], 1);

    const auto second = rpc(_app, client,
                            {{"method", "eval_js"},
                             {"id", 17},
                             {"params", {{"source", "globalThis.counter = (globalThis.counter || 0) + 1"}}}});
    ASSERT_TRUE(second["ok"].get<bool>()) << second.dump();
    EXPECT_EQ(second["result"]["result"], 2);
}

TEST_F(AppAutomationControlJsTest, EvalJsCreatesAndMutatesEntityEndToEnd)
{
    AutomationRpcClient client;
    ASSERT_TRUE(client.connectWithRetry(_port));

    const auto response = rpc(_app, client,
                              {{"method", "eval_js"},
                               {"id", 18},
                               {"params",
                                {{"source",
                                  R"(
                                    const e = ya.entity.create("RpcBox");
                                    const t = e.addComponentByName("TransformComponent");
                                    t.setPosition([3, 4, 5]);
                                    t._rotation = [0, 90, 0];
                                    e.getId()
                                  )"}}}});
    ASSERT_TRUE(response["ok"].get<bool>()) << response.dump();
    const uint32_t id = response["result"]["result"].get<uint32_t>();

    Entity* const entity = _scene->getEntityByEnttID(entt::entity{id});
    ASSERT_NE(entity, nullptr);
    EXPECT_EQ(entity->getName(), "RpcBox");

    TransformComponent* const transform = entity->getComponent<TransformComponent>();
    ASSERT_NE(transform, nullptr);
    EXPECT_EQ(transform->getPosition(), glm::vec3(3.0f, 4.0f, 5.0f));
    EXPECT_EQ(transform->getRotation(), glm::vec3(0.0f, 90.0f, 0.0f));
}

TEST_F(AppAutomationControlJsTest, EvalJsHandleCompositionWorksThroughRpc)
{
    AutomationRpcClient client;
    ASSERT_TRUE(client.connectWithRetry(_port));

    const auto response = rpc(_app, client,
                              {{"method", "eval_js"},
                               {"id", 19},
                               {"params",
                                {{"source",
                                  R"(
                                    const e = ya.entity.create("Composed");
                                    const c = e.components().TransformComponent;
                                    c.setPosition([7, 8, 9]);
                                    [e.getName(), c.getPosition()]
                                  )"}}}});
    ASSERT_TRUE(response["ok"].get<bool>()) << response.dump();
    const auto& result = response["result"]["result"];
    ASSERT_TRUE(result.is_array());
    EXPECT_EQ(result[0], "Composed");
    EXPECT_EQ(result[1], nlohmann::json::array({7.0, 8.0, 9.0}));
}

TEST_F(AppAutomationControlJsTest, EvalJsCallsLibraryNamespaceOverRpc)
{
    AutomationRpcClient client;
    ASSERT_TRUE(client.connectWithRetry(_port));

    const auto response = rpc(_app, client,
                              {{"method", "eval_js"},
                               {"id", 21},
                               {"params",
                                {{"source",
                                  R"(
                                    const types = ya.component.list_types();
                                    const scene = ya.scene.get_active();
                                    [types.includes("TransformComponent"), scene.name, typeof ya.asset.get_info]
                                  )"}}}});
    ASSERT_TRUE(response["ok"].get<bool>()) << response.dump();
    EXPECT_EQ(response["result"]["result"],
              nlohmann::json::array({true, "AutomationJs", "function"}));
}

// ============================================================================
// invoke
// ============================================================================

TEST_F(AppAutomationControlJsTest, InvokeRunsRegistryCommand)
{
    AutomationRpcClient client;
    ASSERT_TRUE(client.connectWithRetry(_port));

    const auto create = rpc(_app, client,
                            {{"method", "invoke"},
                             {"id", 20},
                             {"params", {{"name", "entity.create"}, {"args", {{"name", "Invoked"}}}}}});
    ASSERT_TRUE(create["ok"].get<bool>()) << create.dump();
    const uint32_t id = create["result"]["result"]["id"].get<uint32_t>();
    EXPECT_EQ(create["result"]["result"]["name"], "Invoked");

    const auto set = rpc(_app, client,
                         {{"method", "invoke"},
                          {"id", 21},
                          {"params",
                           {{"name", "component.set"},
                            {"args",
                             {{"id", id},
                              {"type", "TransformComponent"},
                              {"fields", {{"_position", nlohmann::json::array({1.0, 2.0, 3.0})}}}}}}}});
    ASSERT_TRUE(set["ok"].get<bool>()) << set.dump();
    EXPECT_EQ(set["result"]["result"]["_position"], nlohmann::json::array({1.0, 2.0, 3.0}));

    const auto get = rpc(_app, client,
                         {{"method", "invoke"},
                          {"id", 22},
                          {"params", {{"name", "component.get"}, {"args", {{"id", id}, {"type", "TransformComponent"}}}}}});
    ASSERT_TRUE(get["ok"].get<bool>()) << get.dump();
    EXPECT_EQ(get["result"]["result"]["_position"], nlohmann::json::array({1.0, 2.0, 3.0}));

    Entity* const entity = _scene->getEntityByEnttID(entt::entity{id});
    ASSERT_NE(entity, nullptr);
    EXPECT_EQ(entity->getComponent<TransformComponent>()->getPosition(), glm::vec3(1.0f, 2.0f, 3.0f));
}

TEST_F(AppAutomationControlJsTest, InvokeUnknownCommandReturnsError)
{
    AutomationRpcClient client;
    ASSERT_TRUE(client.connectWithRetry(_port));

    const auto response = rpc(_app, client,
                              {{"method", "invoke"},
                               {"id", 23},
                               {"params", {{"name", "no.such_command"}}}});
    ASSERT_TRUE(response.contains("ok"));
    EXPECT_FALSE(response["ok"].get<bool>());
    EXPECT_NE(response["error"].get<std::string>().find("unknown script api"), std::string::npos);
}

TEST_F(AppAutomationControlJsTest, InvokeFailingCommandReturnsError)
{
    AutomationRpcClient client;
    ASSERT_TRUE(client.connectWithRetry(_port));

    const auto response = rpc(_app, client,
                              {{"method", "invoke"},
                               {"id", 24},
                               {"params", {{"name", "entity.get"}, {"args", {{"id", 999999}}}}}});
    ASSERT_TRUE(response.contains("ok"));
    EXPECT_FALSE(response["ok"].get<bool>());
    EXPECT_NE(response["error"].get<std::string>().find("not found"), std::string::npos);
}

// ============================================================================
// list_commands
// ============================================================================

TEST_F(AppAutomationControlJsTest, ListCommandsReturnsCatalog)
{
    AutomationRpcClient client;
    ASSERT_TRUE(client.connectWithRetry(_port));

    const auto response = rpc(_app, client, {{"method", "list_commands"}, {"id", 30}});
    ASSERT_TRUE(response["ok"].get<bool>()) << response.dump();

    const auto& commands = response["result"]["commands"];
    ASSERT_TRUE(commands.is_array());
    EXPECT_GT(commands.size(), 0);

    for (const std::string& expected : {"scene.get_active", "entity.create", "component.list_types", "scene.save"}) {
        const bool found = std::any_of(commands.begin(), commands.end(), [&](const nlohmann::json& c) {
            return c.value("name", "") == expected;
        });
        EXPECT_TRUE(found) << "missing command: " << expected;
    }

    for (const auto& command : commands) {
        EXPECT_TRUE(command.contains("name"));
        EXPECT_TRUE(command.contains("doc"));
        EXPECT_TRUE(command.contains("args"));
    }
}

// ============================================================================
// App scene integration (the RPC surface sees what JS created)
// ============================================================================

TEST_F(AppAutomationControlJsTest, RpcSceneQueriesSeeJsCreatedEntities)
{
    AutomationRpcClient client;
    ASSERT_TRUE(client.connectWithRetry(_port));

    const auto create = rpc(_app, client,
                            {{"method", "eval_js"},
                             {"id", 40},
                             {"params",
                              {{"source",
                                R"(
                                    ya.entity.create("Alpha");
                                    ya.entity.create("Beta");
                                    ya.scene.active().entityCount()
                                )"}}}});
    ASSERT_TRUE(create["ok"].get<bool>()) << create.dump();

    const auto listed = rpc(_app, client, {{"method", "list_scene_entities"}, {"id", 41}});
    ASSERT_TRUE(listed["ok"].get<bool>()) << listed.dump();
    EXPECT_GE(listed["result"]["count"].get<size_t>(), 2);

    const auto info = rpc(_app, client,
                          {{"method", "get_entity_info"},
                           {"id", 42},
                           {"params", {{"name", "Alpha"}}}});
    ASSERT_TRUE(info["ok"].get<bool>()) << info.dump();
    EXPECT_EQ(info["result"]["name"], "Alpha");
    EXPECT_TRUE(info["result"]["components"].is_array());
}

// ============================================================================
// Scripting unavailable
// ============================================================================

class AppAutomationControlNoJsTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        _port = pickFreePort();
        ASSERT_TRUE(_app.getAutomationControlService()->init(_port));
    }

    void TearDown() override { _app.getAutomationControlService()->shutdown(); }

    App      _app;
    uint16_t _port = 0;
};

TEST_F(AppAutomationControlNoJsTest, EvalJsUnavailableWithoutScriptingSystem)
{
    AutomationRpcClient client;
    ASSERT_TRUE(client.connectWithRetry(_port));

    const auto response = rpc(_app, client,
                              {{"method", "eval_js"},
                               {"id", 50},
                               {"params", {{"source", "1 + 1"}}}});
    ASSERT_TRUE(response.contains("ok"));
    EXPECT_FALSE(response["ok"].get<bool>());
    EXPECT_NE(response["error"].get<std::string>().find("js scripting unavailable"), std::string::npos);
}

TEST_F(AppAutomationControlNoJsTest, InvokeUnavailableWithoutScriptingSystem)
{
    AutomationRpcClient client;
    ASSERT_TRUE(client.connectWithRetry(_port));

    const auto response = rpc(_app, client,
                              {{"method", "invoke"},
                               {"id", 51},
                               {"params", {{"name", "entity.create"}}}});
    ASSERT_TRUE(response.contains("ok"));
    EXPECT_FALSE(response["ok"].get<bool>());
    EXPECT_NE(response["error"].get<std::string>().find("js scripting unavailable"), std::string::npos);
}

TEST_F(AppAutomationControlNoJsTest, ListCommandsUnavailableWithoutScriptingSystem)
{
    AutomationRpcClient client;
    ASSERT_TRUE(client.connectWithRetry(_port));

    const auto response = rpc(_app, client, {{"method", "list_commands"}, {"id", 52}});
    ASSERT_TRUE(response.contains("ok"));
    EXPECT_FALSE(response["ok"].get<bool>());
    EXPECT_NE(response["error"].get<std::string>().find("js scripting unavailable"), std::string::npos);
}

} // namespace ya
