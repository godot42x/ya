#pragma once

#include "Core/Api.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

namespace ya
{

class YA_APP_CONTROL_API AppAutomationControlServer
{
  public:
    struct Request
    {
        nlohmann::json id = nullptr;
        std::string    method;
        nlohmann::json params = nlohmann::json::object();

      private:
        friend class AppAutomationControlServer;

        std::mutex              mutex;
        std::condition_variable cv;
        bool                    bCompleted = false;
        nlohmann::json          response;
    };

    using RequestPtr = std::shared_ptr<Request>;

    AppAutomationControlServer();
    AppAutomationControlServer(const AppAutomationControlServer&) = delete;
    AppAutomationControlServer& operator=(const AppAutomationControlServer&) = delete;
    ~AppAutomationControlServer();

    [[nodiscard]] bool init(uint16_t port);
    void shutdown();

    [[nodiscard]] bool isEnabled() const { return _bEnabled; }
    [[nodiscard]] uint16_t getPort() const { return _port; }

    [[nodiscard]] std::deque<RequestPtr> consumePendingRequests();
    void completeRequest(const RequestPtr& request, nlohmann::json response);

  private:
    struct ServerState;

    [[nodiscard]] bool enqueueRequest(RequestPtr request);
    [[nodiscard]] nlohmann::json processRpcLine(const std::string& line);
    void listenerMain();

    std::atomic<bool> _bEnabled       = false;
    std::atomic<bool> _bStopRequested = false;
    uint16_t          _port           = 0;
    std::thread       _listenerThread;
    std::unique_ptr<ServerState> _serverState;

    std::mutex             _incomingMutex;
    std::deque<RequestPtr> _incomingRequests;
};

} // namespace ya
