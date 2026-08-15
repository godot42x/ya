#include "App/Control/AutomationRun.h"

#include "Core/Log.h"

#include <cxxopts.hpp>

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
    // Shared automation CLI parsing through cxxopts (the same library the
    // engine's AppOptions CliParams wraps). Unknown options are ignored so
    // app-specific flags (GUI smokes, project paths, ...) can coexist.
    cxxopts::Options options(argv && argv[0] ? argv[0] : "ya", "YA automation run options");
    options.allow_unrecognised_options();
    options.add_options()
        ("exit-after-frame", "Quit gracefully after rendering N frames", cxxopts::value<uint64_t>()->default_value("0"))
        ("automation-control-port", "Automation control TCP port; 0 disables the server", cxxopts::value<uint16_t>()->default_value("0"));

    try {
        const auto result = options.parse(argc, argv);
        outOptions.exitAfterFrame = result["exit-after-frame"].as<uint64_t>();
        outOptions.controlPort    = result["automation-control-port"].as<uint16_t>();
    }
    catch (const std::exception& e) {
        YA_CORE_WARN("applyAutomationRunArgs: failed to parse automation options: {}", e.what());
    }
}

bool shouldAutomationExitAfterFrame(uint64_t completedFrameCount, uint64_t exitAfterFrame)
{
    return exitAfterFrame > 0 && completedFrameCount >= exitAfterFrame;
}

EAppAutomationExitReason evaluateAutomationExitReason(uint64_t completedFrameCount,
                                                      const AppAutomationRunOptions& options)
{
    return shouldAutomationExitAfterFrame(completedFrameCount, options.exitAfterFrame)
             ? EAppAutomationExitReason::ExitAfterFrame
             : EAppAutomationExitReason::None;
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
