#include "App/Control/AutomationControlServer.h"

#include "Core/Log.h"

#include <asio.hpp>
#include <istream>

namespace ya
{

struct AppAutomationControlServer::ServerState
{
    asio::io_context ioContext;
    /// Bound and listening before the listener thread starts, so a port
    /// conflict is reported by init() rather than discovered later by the client.
    asio::ip::tcp::acceptor acceptor{ioContext};
};

namespace
{
using asio::ip::tcp;
}

AppAutomationControlServer::AppAutomationControlServer() = default;

AppAutomationControlServer::~AppAutomationControlServer()
{
    shutdown();
}

bool AppAutomationControlServer::init(uint16_t port)
{
    shutdown();
    if (port == 0) {
        return true;
    }

    // Reserve the port here, synchronously. The listener thread cannot report a
    // bind failure back to the caller, and a server that silently ends up
    // disabled is worse than no server: the process stays up looking healthy
    // while every automation call fails to connect, so a caller that launched it
    // waiting for a port has no way to tell "still starting" from "never
    // listening". Bind failure has to be the caller's problem to handle.
    _serverState = std::make_unique<ServerState>();

    asio::error_code ec;
    const auto       address = asio::ip::make_address("127.0.0.1", ec);
    if (ec) {
        YA_CORE_ERROR("Automation control server failed to parse listen address: {}", ec.message());
        _serverState.reset();
        return false;
    }

    const asio::ip::tcp::endpoint endpoint(address, port);
    auto&                         acceptor = _serverState->acceptor;
    acceptor.open(endpoint.protocol(), ec);
    if (!ec) {
        // No reuse_address: two instances on one port is the thing worth
        // refusing, and reuse semantics vary per platform.
        acceptor.bind(endpoint, ec);
    }
    if (!ec) {
        acceptor.listen(asio::socket_base::max_listen_connections, ec);
    }
    if (ec) {
        YA_CORE_ERROR("Automation control server failed to listen on 127.0.0.1:{}: {}",
                      port,
                      ec.message());
        asio::error_code closeEc;
        acceptor.close(closeEc);
        _serverState.reset();
        return false;
    }

    _port           = port;
    _bStopRequested = false;
    _bEnabled       = true;
    _listenerThread = std::thread([this]()
                                  { listenerMain(); });
    YA_CORE_INFO("Automation control server listening on 127.0.0.1:{}", _port);
    return true;
}

void AppAutomationControlServer::shutdown()
{
    _bStopRequested = true;
    _bEnabled       = false;

    if (_serverState) {
        asio::error_code ec;
        if (_serverState->acceptor.is_open()) {
            _serverState->acceptor.cancel(ec);
            _serverState->acceptor.close(ec);
        }
        _serverState->ioContext.stop();
    }

    if (_listenerThread.joinable()) {
        _listenerThread.join();
    }

    std::deque<RequestPtr> pending;
    {
        std::scoped_lock lock(_incomingMutex);
        pending.swap(_incomingRequests);
    }

    for (auto& request : pending) {
        completeRequest(request,
                        {
                            {"id", request->id},
                            {"ok", false},
                            {"error", "automation control server shutting down"},
                        });
    }

    _port = 0;
    _serverState.reset();
}

std::deque<AppAutomationControlServer::RequestPtr> AppAutomationControlServer::consumePendingRequests()
{
    std::deque<RequestPtr> incoming;
    {
        std::scoped_lock lock(_incomingMutex);
        incoming.swap(_incomingRequests);
    }
    return incoming;
}

void AppAutomationControlServer::completeRequest(const RequestPtr& request, nlohmann::json response)
{
    {
        std::scoped_lock lock(request->mutex);
        request->response   = std::move(response);
        request->bCompleted = true;
    }
    request->cv.notify_one();
}

bool AppAutomationControlServer::enqueueRequest(RequestPtr request)
{
    if (!_bEnabled || _bStopRequested) {
        return false;
    }

    std::scoped_lock lock(_incomingMutex);
    _incomingRequests.push_back(std::move(request));
    return true;
}

void AppAutomationControlServer::listenerMain()
{
    if (!_serverState) {
        YA_CORE_ERROR("Automation control server missing runtime state");
        _bEnabled = false;
        return;
    }

    auto&            serverState = *_serverState;
    asio::error_code ec;
    while (!_bStopRequested) {
        tcp::socket clientSocket(serverState.ioContext);
        serverState.acceptor.accept(clientSocket, ec);
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

nlohmann::json AppAutomationControlServer::processRpcLine(const std::string& line)
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

    auto pending    = std::make_shared<Request>();
    pending->id     = request.value("id", nlohmann::json(nullptr));
    pending->method = request["method"].get<std::string>();
    if (request.contains("params")) {
        pending->params = request["params"];
    }

    if (!enqueueRequest(pending)) {
        return {
            {"id", pending->id},
            {"ok", false},
            {"error", "automation control server is not accepting requests"},
        };
    }

    std::unique_lock lock(pending->mutex);
    pending->cv.wait(lock, [&]()
                     { return pending->bCompleted || _bStopRequested.load(); });
    if (!pending->bCompleted) {
        return {
            {"id", pending->id},
            {"ok", false},
            {"error", "automation control request interrupted"},
        };
    }
    return pending->response;
}

} // namespace ya
