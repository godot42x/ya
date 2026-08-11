#include "Core/Application/AutomationRun.h"

#include <cstdlib>
#include <string>

namespace ya
{

AppAutomationRunController::AppAutomationRunController(const AppAutomationRunOptions& options)
{
    reset(options);
}

void AppAutomationRunController::reset(const AppAutomationRunOptions& options)
{
    _options = options;
    _state   = {};
}

void AppAutomationRunController::markFrameCompleted()
{
    ++_state.completedFrameCount;
    if (_state.exitReason != EAppAutomationExitReason::None) {
        return;
    }
    if (shouldAutomationExitAfterFrame(_state.completedFrameCount, _options.exitAfterFrame)) {
        _state.exitReason = EAppAutomationExitReason::ExitAfterFrame;
    }
}

void AppAutomationRunController::requestAppClose()
{
    if (_state.exitReason != EAppAutomationExitReason::None) {
        return;
    }
    _state.exitReason = EAppAutomationExitReason::AppRequestedClose;
}

void AppAutomationRunController::requestRemoteQuit()
{
    if (_state.exitReason != EAppAutomationExitReason::None) {
        return;
    }
    _state.exitReason = EAppAutomationExitReason::RemoteQuit;
}

bool AppAutomationRunController::shouldExit() const
{
    return _state.exitReason != EAppAutomationExitReason::None;
}

uint64_t AppAutomationRunController::getCompletedFrameCount() const
{
    return _state.completedFrameCount;
}

EAppAutomationExitReason AppAutomationRunController::getExitReason() const
{
    return _state.exitReason;
}

const AppAutomationRunOptions& AppAutomationRunController::getOptions() const
{
    return _options;
}

void applyAutomationRunArgs(int argc, char** argv, AppAutomationRunOptions& outOptions)
{
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--exit-after-frame") {
            if (i + 1 < argc) {
                outOptions.exitAfterFrame = static_cast<uint64_t>(std::strtoull(argv[++i], nullptr, 10));
            }
            continue;
        }
        if (arg.starts_with("--exit-after-frame=")) {
            outOptions.exitAfterFrame = static_cast<uint64_t>(
                std::strtoull(arg.c_str() + std::string("--exit-after-frame=").size(), nullptr, 10));
            continue;
        }
        if (arg == "--automation-control-port") {
            if (i + 1 < argc) {
                outOptions.controlPort = static_cast<uint16_t>(std::strtoul(argv[++i], nullptr, 10));
            }
            continue;
        }
        if (arg.starts_with("--automation-control-port=")) {
            outOptions.controlPort = static_cast<uint16_t>(
                std::strtoul(arg.c_str() + std::string("--automation-control-port=").size(), nullptr, 10));
        }
    }
}

bool shouldAutomationExitAfterFrame(uint64_t completedFrameCount, uint64_t exitAfterFrame)
{
    return exitAfterFrame > 0 && completedFrameCount >= exitAfterFrame;
}

const char* getAutomationExitReasonName(EAppAutomationExitReason reason)
{
    switch (reason) {
    case EAppAutomationExitReason::AppRequestedClose:
        return "app-requested-close";
    case EAppAutomationExitReason::RemoteQuit:
        return "remote-quit";
    case EAppAutomationExitReason::ExitAfterFrame:
        return "exit-after-frame";
    case EAppAutomationExitReason::None:
    default:
        return "none";
    }
}

} // namespace ya
