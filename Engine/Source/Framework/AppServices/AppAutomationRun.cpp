#include "AppServices/AppAutomationRun.h"

#include <cstdlib>
#include <string>

namespace ya
{

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
        }
    }
}

bool shouldAutomationExitAfterFrame(uint64_t completedFrameCount, uint64_t exitAfterFrame)
{
    return exitAfterFrame > 0 && completedFrameCount >= exitAfterFrame;
}

} // namespace ya
