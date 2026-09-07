#include "GUI/Compose/UIFrameComposeReplay.h"

namespace ya
{

FUIFrameComposeReplayStats measureUIFrameComposeReplay(const UIFrameSnapshot& snapshot)
{
    FUIFrameComposeReplayStats stats;
    walkComposeClipRuns(snapshot, [](const UIFrameDrawItem&) {}, &stats);
    return stats;
}

} // namespace ya
