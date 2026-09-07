#include "GUI/Compose/UIFrameComposeReplay.h"

namespace ya
{

FUIFrameComposeReplayStats measureUIFrameComposeReplay(const UIFrameSnapshot& snapshot)
{
    FUIFrameComposeReplayStats stats;
    bool                       bPendingScreenBatch = false;
    FComposeScissorState       active;

    const auto flushIfPending = [&]() {
        if (!bPendingScreenBatch) {
            return;
        }
        ++stats.screenFlushCount;
        bPendingScreenBatch = false;
    };

    const auto applyScissor = [&](const FComposeScissorState& next) {
        if (sameComposeScissor(next, active)) {
            return;
        }
        ++stats.scissorTransitionCount;
        stats.scissorSequence.push_back(next);
        active = next;
    };

    // Mirrors `replaySnapshotItems`: clipped items always push/pop around the
    // emit. Render2D flushes pending screen quads on clip change and on pop,
    // then again at session end if geometry remains.
    for (const auto& item : snapshot.items) {
        ++stats.itemCount;
        stats.painterOrder.push_back(item.kind);

        if (item.bClipped) {
            ++stats.clippedItemCount;
            const FComposeScissorState incoming = composeScissorOf(item);
            if (!sameComposeScissor(incoming, active)) {
                flushIfPending();
            }
            ++stats.clipPushCount;
            applyScissor(incoming);

            bPendingScreenBatch = true;

            flushIfPending();
            ++stats.clipPopCount;
            applyScissor(FComposeScissorState{});
            continue;
        }

        bPendingScreenBatch = true;
    }

    flushIfPending();
    return stats;
}

} // namespace ya
