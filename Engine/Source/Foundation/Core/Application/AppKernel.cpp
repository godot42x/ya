#include "Core/Application/AppKernel.h"

#include <algorithm>
#include <chrono>

namespace ya
{

AppKernel::AppKernel(Config config, IAppLoopDelegate& delegate)
    : _config(config)
    , _delegate(delegate)
{
}

AppKernel::~AppKernel()
{
    if (_bDelegateStarted) {
        _delegate.onShutdown();
        _bDelegateStarted = false;
    }
}

int AppKernel::run(const AppAutomationRunOptions& options)
{
    _runController.reset(options);
    _delegate.onInit();
    _bDelegateStarted = true;

    using clock = std::chrono::steady_clock;
    auto last   = clock::now();
    int  result = 0;
    do {
        const auto now   = clock::now();
        const float dt   = std::max(0.0001f,
                                    std::chrono::duration<float>(now - last).count());
        last             = now;
        result           = iterate(dt);
    } while (result == 0);

    _delegate.onShutdown();
    _bDelegateStarted = false;
    return result;
}

int AppKernel::iterate(float dt)
{
    if (_config.eventSource) {
        _config.eventSource->pollEvents(
            [this](const Event& event) { _delegate.onEvent(event); });
    }

    if (_config.frameSink && !_config.frameSink->beginFrame()) {
        return 0;
    }

    _delegate.onTick(dt);

    if (_config.frameSink) {
        _config.frameSink->endFrame();
    }

    _runController.markFrameCompleted();
    if (_delegate.shouldClose()) {
        _runController.requestAppClose();
    }
    return _runController.shouldExit() ? 1 : 0;
}

} // namespace ya
