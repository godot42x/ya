#pragma once

#include "Core/Api.h"
#include "GUI/Widgets/UIFrameSnapshot.h"

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

/// CPU-only observation of the compose clip protocol used by
/// `replaySnapshotItems`. Does not record GPU commands or mutate the packet.
///
/// Current protocol (GAH-001 baseline): each clipped item does
/// `pushClip -> emit -> popClip`. `Render2D::popClipRect` flushes the pending
/// screen batch, so N adjacent items that share one flattened clip still
/// produce N screen flushes. GAH-101 must keep this function in lockstep with
/// `replaySnapshotItems` when clip-run batching lands.
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

[[nodiscard]] YA_GUI_API FUIFrameComposeReplayStats
measureUIFrameComposeReplay(const UIFrameSnapshot& snapshot);

} // namespace ya
