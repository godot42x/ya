#pragma once

#include "Core/Api.h"

#include <cstdint>

namespace ya
{

struct YA_CORE_API AppAutomationRunOptions
{
    uint64_t exitAfterFrame = 0;
    uint16_t controlPort    = 0;
};

enum class EAppAutomationExitReason : uint8_t
{
    None = 0,
    AppRequestedClose,
    RemoteQuit,
    ExitAfterFrame,
};

struct YA_CORE_API AppAutomationRunState
{
    uint64_t                 completedFrameCount = 0;
    EAppAutomationExitReason exitReason          = EAppAutomationExitReason::None;
};

YA_CORE_API void applyAutomationRunArgs(int argc, char** argv, AppAutomationRunOptions& outOptions);

class YA_CORE_API AppAutomationRunController
{
public:
    AppAutomationRunController() = default;
    explicit AppAutomationRunController(const AppAutomationRunOptions& options);

    void reset(const AppAutomationRunOptions& options = {});
    void markFrameCompleted();
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

[[nodiscard]] YA_CORE_API bool shouldAutomationExitAfterFrame(uint64_t completedFrameCount,
                                                              uint64_t exitAfterFrame);
[[nodiscard]] YA_CORE_API EAppAutomationExitReason evaluateAutomationExitReason(uint64_t completedFrameCount,
                                                                                const AppAutomationRunOptions& options);
[[nodiscard]] YA_CORE_API const char* getAutomationExitReasonName(EAppAutomationExitReason reason);

} // namespace ya
