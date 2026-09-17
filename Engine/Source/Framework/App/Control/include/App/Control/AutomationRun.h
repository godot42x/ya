#pragma once

#include "Core/Api.h"

#include <chrono>
#include <cstdint>

namespace ya
{

struct YA_APP_CONTROL_API AppAutomationRunOptions
{
    uint64_t exitAfterTick = 0;
    uint16_t controlPort    = 0;
    /// Wall-clock ceiling for the whole run; 0 = unlimited.
    ///
    /// exitAfterTick counts frames, so it cannot stop a process that stalls, and
    /// it says nothing about how long a run may sit idle. An unattended instance
    /// has to have a deadline of its own, otherwise "forgot to close it" is
    /// unbounded: it holds the GPU, the port and the build outputs until someone
    /// notices.
    double maxLifetimeSeconds = 0.0;
};

enum class EAppAutomationExitReason : uint8_t
{
    None = 0,
    AppRequestedClose,
    RemoteQuit,
    ExitAfterTick,
    MaxLifetime,
};

struct YA_APP_CONTROL_API AppAutomationRunState
{
    uint64_t                 completedTickCount  = 0;
    EAppAutomationExitReason exitReason          = EAppAutomationExitReason::None;
};

YA_APP_CONTROL_API void applyAutomationRunArgs(int argc, char** argv, AppAutomationRunOptions& outOptions);

class YA_APP_CONTROL_API AppAutomationRunController
{
public:
    AppAutomationRunController() = default;
    explicit AppAutomationRunController(const AppAutomationRunOptions& options);

    void reset(const AppAutomationRunOptions& options = {});
    void markTickCompleted();
    void requestAppClose();
    void requestRemoteQuit();

    [[nodiscard]] bool shouldExit() const;
    [[nodiscard]] uint64_t getCompletedFrameCount() const;
    [[nodiscard]] EAppAutomationExitReason getExitReason() const;
    [[nodiscard]] const AppAutomationRunOptions& getOptions() const;
    /// Seconds since reset(). Drives the maxLifetimeSeconds deadline.
    [[nodiscard]] double getElapsedSeconds() const;

private:
    AppAutomationRunOptions _options{};
    AppAutomationRunState   _state{};
    std::chrono::steady_clock::time_point _startTime = std::chrono::steady_clock::now();
};

[[nodiscard]] YA_APP_CONTROL_API bool shouldAutomationExitAfterTick(uint64_t completedTickCount,
                                                              uint64_t exitAfterTick);
[[nodiscard]] YA_APP_CONTROL_API bool shouldAutomationExitAfterLifetime(double elapsedSeconds,
                                                                  double maxLifetimeSeconds);
[[nodiscard]] YA_APP_CONTROL_API EAppAutomationExitReason evaluateAutomationExitReason(uint64_t completedTickCount,
                                                                                const AppAutomationRunOptions& options);
[[nodiscard]] YA_APP_CONTROL_API const char* getAutomationExitReasonName(EAppAutomationExitReason reason);

} // namespace ya
