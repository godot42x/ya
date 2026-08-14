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

/// Optional presentation policy. Null = no presentation (server / CLI).
/// The kernel never interprets it: it only sequences begin/tick/end.
struct IAppFrameSink
{
    virtual ~IAppFrameSink() = default;
    virtual bool     beginFrame() = 0;   // false = skip this frame
    virtual void     endFrame()   = 0;   // present / readback
    virtual Extent2D getExtent() const = 0;
};

class YA_APP_KERNEL_API AppKernel
{
public:
    struct Config
    {
        IAppEventSource* eventSource = nullptr;
        IAppFrameSink*   frameSink   = nullptr;
    };

    AppKernel(Config config, IAppLoopDelegate& delegate);
    ~AppKernel();

    /// Run until delegate close / exit policy. Calls onInit/onShutdown.
    int run(const AppAutomationRunOptions& options = {});
    /// Advance one frame; returns 0 to continue, non-zero to exit.
    int iterate(float dt);

private:
    Config            _config;
    IAppLoopDelegate& _delegate;
    AppAutomationRunController _runController;
    bool _bDelegateStarted = false;
};

} // namespace ya
