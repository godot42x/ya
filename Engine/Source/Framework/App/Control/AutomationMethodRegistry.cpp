#include "App/Control/AutomationMethodRegistry.h"

#include <utility>

namespace ya
{

void AutomationMethodRegistry::add(std::string method, Handler handler)
{
    _methods.insert_or_assign(std::move(method), std::move(handler));
}

void AutomationMethodRegistry::dispatch(const AppAutomationControlServer::RequestPtr& request,
                                        AppAutomationControlServer&                   server) const
{
    const auto it = _methods.find(request->method);
    if (it == _methods.end()) {
        // Unknown methods must never hang the client: the pumping loop owns
        // the server, so it completes the request right here.
        server.completeRequest(request,
                               makeAutomationError(*request, "unknown method: " + request->method));
        return;
    }
    it->second(request);
}

nlohmann::json makeAutomationSuccess(const AppAutomationControlServer::Request& request,
                                     nlohmann::json                             result)
{
    return {
        {"id", request.id},
        {"ok", true},
        {"result", std::move(result)},
    };
}

nlohmann::json makeAutomationError(const AppAutomationControlServer::Request& request,
                                   std::string_view                           message)
{
    return {
        {"id", request.id},
        {"ok", false},
        {"error", std::string(message)},
    };
}

} // namespace ya
