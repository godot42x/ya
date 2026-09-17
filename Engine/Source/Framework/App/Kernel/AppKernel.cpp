#include "App/Kernel/AppKernel.h"

#include "Core/Log.h"
#include "Core/Os/InstanceRegistry.h"

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
    // The kernel owns the claim, so it also owns the record's lifetime: a record
    // describes a live instance, and the claim is what makes it live. The
    // product publishes the content (only it knows the project and the mode).
    if (!_config.instanceKey.empty()) {
        uint32_t         ownerPid = 0;
        const std::filesystem::path lockPath = Os::ProcessLock::resolveLockPath(_config.instanceKey);
        if (!_instanceLock.tryAcquire(_config.instanceKey, ownerPid)) {
            const auto record = Os::readInstanceRecord(_config.instanceKey);
            if (record && record->pid != 0 && record->controlPort != 0) {
                YA_CORE_ERROR("Refusing to start '{}': already running as pid {} (automation control "
                              "port {}). Attach to it instead of starting another one, or stop it "
                              "first.",
                              _config.instanceKey,
                              record->pid,
                              record->controlPort);
            }
            else if (record && record->pid != 0) {
                YA_CORE_ERROR("Refusing to start '{}': already running as pid {}. Stop it first, or "
                              "use a different instance key.",
                              _config.instanceKey,
                              record->pid);
            }
            else if (ownerPid != 0) {
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

    // Drop the claim and its record together, and in that order: the instance
    // stops being reachable only once it stops being exclusive, so a client that
    // reads the record between these two steps finds a claim it cannot take.
    _instanceLock.release();
    Os::removeInstanceRecord(_config.instanceKey);
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
