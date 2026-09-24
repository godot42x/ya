#pragma once

#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/RenderSurfaceContext.h"
#include "RHI/Render.h"

#include <cstdint>
#include <vector>

namespace ya
{

/// One acquired present attempt on a single `IRenderSurfaceContext`.
/// Recreate / zero-extent / minimize stay inside `begin()`; this is only the
/// host/coordinator pairing of acquire → record → present.
struct FPresentFrame
{
    IRenderSurfaceContext* surface    = nullptr;
    int32_t                imageIndex = -1;

    [[nodiscard]] bool acquired() const { return surface != nullptr && imageIndex >= 0; }
};

/// Wait in-flight, apply dirty recreate, acquire. `acquired()` is false when
/// the surface is unpresentable (`imageIndex == -1`); that is not a failed
/// begin. Returns false only when acquire itself failed.
[[nodiscard]] inline bool acquirePresentFrame(FPresentFrame& frame)
{
    frame.imageIndex = -1;
    if (!frame.surface) {
        return false;
    }
    return frame.surface->begin(&frame.imageIndex);
}

/// What one acquired surface contributes to the frame's submission.
///
/// These are semaphores, not a submission: the frame submits command buffers to
/// a queue (`IRender::submitFrame`) and each acquired image contributes "wait
/// until this image is free to write" plus "signal when it is ready to
/// present". Naming them here is what lets one submission carry several
/// surfaces instead of each window submitting for itself.
struct FPresentSync
{
    std::vector<void*> waits;
    std::vector<void*> signals;
};

[[nodiscard]] inline FPresentSync presentSyncOf(IRenderSurfaceContext& surface, int32_t imageIndex)
{
    if (imageIndex < 0) {
        return {};
    }
    return FPresentSync{
        .waits   = {surface.getCurrentImageAvailableSemaphore()},
        .signals = {surface.getRenderFinishedSemaphore(static_cast<uint32_t>(imageIndex))},
    };
}

/// Submit `commandBuffers` as the work that fills the acquired image, then
/// present it. An empty command list still legalizes the image (see
/// `presentFallbackCommand`), and `imageIndex < 0` is a no-op for both steps.
///
/// This is the single-window spelling of submit-then-present. A frame that
/// presents several windows builds ONE submission from every acquired surface's
/// `presentSyncOf` and calls `present` per surface afterwards.
inline bool submitPresentFrame(IRender& render, FPresentFrame& frame, std::vector<void*> commandBuffers)
{
    if (!frame.surface) {
        return false;
    }
    const int32_t imageIndex = frame.imageIndex;
    frame.imageIndex         = -1;
    if (imageIndex < 0) {
        return true;
    }

    std::vector<void*> submits = std::move(commandBuffers);
    if (submits.empty()) {
        // Nothing filled this image, but it was acquired and must still reach the
        // display: the barrier is what makes that legal.
        ICommandBuffer* barrier = frame.surface->presentFallbackCommand(static_cast<uint32_t>(imageIndex));
        if (!barrier) {
            return false;
        }
        submits.push_back(barrier->getHandle());
    }

    const FPresentSync sync = presentSyncOf(*frame.surface, imageIndex);
    return render.submitFrame(submits, sync.waits, sync.signals) &&
           frame.surface->present(imageIndex);
}

} // namespace ya
