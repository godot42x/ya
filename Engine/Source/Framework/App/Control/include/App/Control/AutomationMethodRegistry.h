#pragma once

#include "Core/Api.h"
#include "App/Control/AutomationControlServer.h"

#include <functional>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace ya
{

/// Method dispatch for the automation control plane. The server moves bytes;
/// this table owns the "which method runs" contract: each layer registers the
/// methods it answers (the framework host its generic GUI verbs, the product
/// its scene/render verbs) and the frame loop dispatches instead of growing a
/// per-product if-chain. A later registration for the same method replaces
/// the earlier one, so a product can override a framework default.
class YA_APP_CONTROL_API AutomationMethodRegistry
{
  public:
    using Handler = std::function<void(const AppAutomationControlServer::RequestPtr&)>;

    void add(std::string method, Handler handler);

    /// Runs the handler registered for `request->method`. An unknown method is
    /// completed with the shared error response through `server`, so callers
    /// never branch on dispatch results.
    void dispatch(const AppAutomationControlServer::RequestPtr& request,
                  AppAutomationControlServer&                   server) const;

  private:
    std::unordered_map<std::string, Handler> _methods;
};

/// The success/error envelopes every automation responder speaks. One shape,
/// one place: {"id", "ok", "result"} / {"id", "ok", "error"}.
[[nodiscard]] YA_APP_CONTROL_API nlohmann::json makeAutomationSuccess(
    const AppAutomationControlServer::Request& request,
    nlohmann::json                             result = nlohmann::json::object());

[[nodiscard]] YA_APP_CONTROL_API nlohmann::json makeAutomationError(
    const AppAutomationControlServer::Request& request,
    std::string_view                           message);

} // namespace ya
