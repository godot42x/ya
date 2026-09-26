#pragma once

#include "Core/Api.h"
#include "GUI/Widgets/UIFrameSnapshot.h"

#include <functional>
#include <vector>

namespace ya
{

/// Effective compose scissor after flattening inherited clips into each
/// draw item. Unclipped items use `bClipped == false`; `clip` is ignored.
struct FComposeScissorState
{
    bool   bClipped = false;
    Rect2D clip{};
};

/// Compare flattened clip by flag + rect, never by object representation.
[[nodiscard]] inline bool sameComposeScissor(const FComposeScissorState& a,
                                             const FComposeScissorState& b)
{
    if (a.bClipped != b.bClipped) {
        return false;
    }
    if (!a.bClipped) {
        return true;
    }
    return a.clip.pos == b.clip.pos && a.clip.extent == b.clip.extent;
}

[[nodiscard]] inline FComposeScissorState composeScissorOf(const UIFrameDrawItem& item)
{
    FComposeScissorState state;
    state.bClipped = item.bClipped;
    if (item.bClipped) {
        state.clip = item.clip;
    }
    return state;
}

/// GPU clip stack hooks. Null hooks make this a CPU-only walk
/// (`measureUIFrameComposeReplay`). Texture/capacity overflow flushes inside
/// Render2D are not modeled here.
struct FComposeClipRunSink
{
    std::function<void(const Rect2D& clip)> pushClip;
    std::function<void()>                   popClip;
};

/// CPU-only observation of the compose clip-run protocol. Adjacent items that
/// share one flattened clip keep a single scissor (one push, one pop, one
/// screen flush aside from overflow). Does not record GPU commands or mutate
/// the packet.
struct FUIFrameComposeReplayStats
{
    uint32_t itemCount              = 0;
    uint32_t clippedItemCount       = 0;
    uint32_t clipPushCount          = 0;
    uint32_t clipPopCount           = 0;
    uint32_t scissorTransitionCount = 0;
    uint32_t screenFlushCount       = 0;
    std::vector<UIFrameDrawItem::EKind> painterOrder;
    std::vector<FComposeScissorState>   scissorSequence;
};

/// Shared clip-run walker for measure and `replaySnapshotItems`. Painter order
/// is preserved; clip A -> clip B does not record an intermediate unclipped
/// scissor because nothing is emitted between the pop and the next push.
template <typename TEmit>
void walkComposeClipRuns(const UIFrameSnapshot&    snapshot,
                         TEmit&&                   emit,
                         FUIFrameComposeReplayStats* stats = nullptr,
                         const FComposeClipRunSink&  sink  = {})
{
    FComposeScissorState active{};
    bool                 bPendingScreenBatch = false;

    const auto noteFlush = [&]() {
        if (!bPendingScreenBatch) {
            return;
        }
        if (stats != nullptr) {
            ++stats->screenFlushCount;
        }
        bPendingScreenBatch = false;
    };

    const auto recordScissor = [&](const FComposeScissorState& next) {
        if (stats == nullptr) {
            return;
        }
        ++stats->scissorTransitionCount;
        stats->scissorSequence.push_back(next);
    };

    const auto setActiveClip = [&](const FComposeScissorState& next) {
        if (sameComposeScissor(next, active)) {
            return;
        }

        if (active.bClipped) {
            noteFlush();
            if (stats != nullptr) {
                ++stats->clipPopCount;
            }
            if (sink.popClip != nullptr) {
                sink.popClip();
            }
        }
        else {
            noteFlush();
        }

        if (next.bClipped) {
            if (stats != nullptr) {
                ++stats->clipPushCount;
            }
            if (sink.pushClip != nullptr) {
                sink.pushClip(next.clip);
            }
        }

        recordScissor(next);
        active = next;
    };

    for (const auto& item : snapshot.items) {
        if (stats != nullptr) {
            ++stats->itemCount;
            stats->painterOrder.push_back(item.kind);
            if (item.bClipped) {
                ++stats->clippedItemCount;
            }
        }
        setActiveClip(composeScissorOf(item));
        emit(item);
        bPendingScreenBatch = true;
    }

    if (active.bClipped) {
        noteFlush();
        if (stats != nullptr) {
            ++stats->clipPopCount;
        }
        if (sink.popClip != nullptr) {
            sink.popClip();
        }
        recordScissor(FComposeScissorState{});
        active = {};
    }
    else {
        // Models Render2D::end() flushing leftover unclipped geometry.
        noteFlush();
    }
}

[[nodiscard]] YA_GUI_API FUIFrameComposeReplayStats
measureUIFrameComposeReplay(const UIFrameSnapshot& snapshot);

} // namespace ya
