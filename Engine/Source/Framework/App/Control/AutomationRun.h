#pragma once

#include "Core/Api.h"

#include <cstdint>

namespace ya
{

struct YA_APP_CONTROL_API AppAutomationRunOptions
{
    uint64_t exitAfterTick = 0;
    uint16_t controlPort    = 0;
};

enum class EAppAutomationExitReason : uint8_t
{
    None = 0,
    AppRequestedClose,
    RemoteQuit,
    ExitAfterTick,
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

private:
    AppAutomationRunOptions _options{};
    AppAutomationRunState   _state{};
};

[[nodiscard]] YA_APP_CONTROL_API bool shouldAutomationExitAfterTick(uint64_t completedTickCount,
                                                              uint64_t exitAfterTick);
[[nodiscard]] YA_APP_CONTROL_API EAppAutomationExitReason evaluateAutomationExitReason(uint64_t completedTickCount,
                                                                                const AppAutomationRunOptions& options);
[[nodiscard]] YA_APP_CONTROL_API const char* getAutomationExitReasonName(EAppAutomationExitReason reason);

} // namespace ya
