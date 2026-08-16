#pragma once

// Single native app loop, shared by game engine / game / GUI app / headless
// tools / dedicated servers. The kernel owns ONLY: event pump, frame timing,
// delegate ticks, and the shared exit/automation policy. It knows nothing
// about presentation: a frame sink is optional and injected by the product
// line, so a CLI or server simply runs with a null sink.

#include "Core/Api.h"
#include "App/Control/AutomationRun.h"
#include "Core/Common/Types.h"
#include "Core/Event.h"

#include <functional>
#include <vector>

namespace ya
{

/// Produces native/scripted events for the loop. Null = no input (headless).
struct IAppEventSource
{
    virtual ~IAppEventSource() = default;
    virtual void pollEvents(const std::function<void(const Event&)>& emit) = 0;
};

/// Product-line logic injected into the single loop.
struct IAppLoopDelegate
{
    virtual ~IAppLoopDelegate() = default;
    virtual void onInit()               = 0;
    virtual void onEvent(const Event&)  = 0;
    virtual void onTick(float dt)       = 0;
    virtual void onShutdown()           = 0;
    virtual bool shouldClose() const { return false; }
};

class YA_APP_KERNEL_API AppKernel
{
public:
    struct Config
    {
        IAppEventSource* eventSource = nullptr;
    };

    AppKernel(Config config, IAppLoopDelegate& delegate);
    ~AppKernel();

    /// Run until delegate close / exit policy. Calls onInit/onShutdown.
    int run(const AppAutomationRunOptions& options = {});

private:
    /// Advance one frame; returns 0 to continue, non-zero to exit.
    int iterate(float dt);

    Config            _config;
    IAppLoopDelegate& _delegate;
    AppAutomationRunController _runController;
    bool _bDelegateStarted = false;
};

} // namespace ya
