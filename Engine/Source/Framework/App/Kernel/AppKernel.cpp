#include "App/Kernel/AppKernel.h"

#include "Core/Log.h"
#include "Core/Os/InstanceRegistry.h"

#include <algorithm>
#include <chrono>

namespace ya
{

namespace
{

/// The claim can be refused for several reasons; they differ only in which
/// fact is available to name (record with port, record without, pid from the
/// lock, or just the lock path). One message per fact, one place.
void reportInstanceConflict(const std::string&                 key,
                            const std::optional<Os::FInstanceRecord>& record,
                            uint32_t                         ownerPid)
{
    const std::filesystem::path lockPath = Os::ProcessLock::resolveLockPath(key);
    if (record && record->pid != 0 && record->controlPort != 0) {
        YA_CORE_ERROR("Refusing to start '{}': already running as pid {} (automation control "
                      "port {}). Attach to it instead of starting another one, or stop it "
                      "first.",
                      key,
                      record->pid,
                      record->controlPort);
    }
    else if (record && record->pid != 0) {
        YA_CORE_ERROR("Refusing to start '{}': already running as pid {}. Stop it first, or "
                      "use a different instance key.",
                      key,
                      record->pid);
    }
    else if (ownerPid != 0) {
        YA_CORE_ERROR("Refusing to start '{}': already running as pid {}. Stop it first, or "
                      "use a different instance key.",
                      key,
                      ownerPid);
    }
    else {
        YA_CORE_ERROR("Refusing to start '{}': another live process holds '{}'. Stop it "
                      "first, or use a different instance key.",
                      key,
                      lockPath.string());
    }
}

} // namespace

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
    // Order matters on both ends: the record goes out only after the claim is
    // won, and it is removed only after the claim is dropped -- a client that
    // reads the record in between finds a claim it cannot take.
    const std::string& instanceKey = _config.instanceRecord.key;
    if (!instanceKey.empty()) {
        uint32_t ownerPid = 0;
        if (!_instanceLock.tryAcquire(instanceKey, ownerPid)) {
            const auto record = Os::readInstanceRecord(instanceKey);
            reportInstanceConflict(instanceKey, record, ownerPid);
            return 1;
        }
        Os::writeInstanceRecord(_config.instanceRecord);
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

    _instanceLock.release();
    if (!instanceKey.empty()) {
        Os::removeInstanceRecord(instanceKey);
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
