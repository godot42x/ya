#include "App/Kernel/AppKernel.h"

#include "Core/Log.h"

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
    if (!_config.instanceKey.empty()) {
        uint32_t         ownerPid = 0;
        const std::filesystem::path lockPath = Os::ProcessLock::resolveLockPath(_config.instanceKey);
        if (!_instanceLock.tryAcquire(_config.instanceKey, ownerPid)) {
            if (ownerPid != 0) {
                YA_CORE_ERROR("Refusing to start '{}': already running as pid {}. Stop it first, or "
                              "use a different instance key.",
                              _config.instanceKey,
                              ownerPid);
            }
            else {
                YA_CORE_ERROR("Refusing to start '{}': another live process holds '{}'. Stop it "
                              "first, or use a different instance key.",
                              _config.instanceKey,
                              lockPath.string());
            }
            return 1;
        }
    }

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

    if (_runController.getExitReason() != EAppAutomationExitReason::None) {
        YA_CORE_INFO("App loop exited: reason={} after {} frames, {:.1f}s",
                     getAutomationExitReasonName(_runController.getExitReason()),
                     _runController.getCompletedFrameCount(),
                     _runController.getElapsedSeconds());
    }
    return 0;
}

int AppKernel::iterate(float dt)
{
    if (_config.eventSource) {
        _config.eventSource->pollEvents(
            [this](const Event& event) { _delegate.onEvent(event); });
    }

    _delegate.onTick(dt);

    _runController.markTickCompleted();
    if (_delegate.shouldClose()) {
        _runController.requestAppClose();
    }
    return _runController.shouldExit() ? 1 : 0;
}

} // namespace ya
