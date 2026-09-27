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

/// One tick's queue submission: every acquired surface's commands and sync
/// pair, submitted once, then presented per surface.
///
/// A surface whose image was not acquired (`imageIndex < 0`) contributes
/// nothing, so a minimized window does not add a wait that never signals.
/// An acquired image with no recorded commands is legalized by that surface's
/// `presentFallbackCommand` and still joins this submission. An empty
/// submission does not call `submitFrame`: the frame fence stays signaled.
struct FFrameSubmission
{
    std::vector<void*> commandBuffers;
    FPresentSync       sync;

    struct Present
    {
        IRenderSurfaceContext* surface    = nullptr;
        int32_t                imageIndex = -1;
    };
    std::vector<Present> presents;

    /// Consume `frame`. Returns false when the surface is missing, or when an
    /// acquired image cannot be legalized. `imageIndex < 0` succeeds and adds
    /// nothing.
    bool add(FPresentFrame& frame, std::vector<void*> commands)
    {
        if (!frame.surface) {
            return false;
        }
        const int32_t imageIndex = frame.imageIndex;
        frame.imageIndex         = -1;
        if (imageIndex < 0) {
            return true;
        }

        if (commands.empty()) {
            ICommandBuffer* barrier = frame.surface->presentFallbackCommand(static_cast<uint32_t>(imageIndex));
            if (!barrier) {
                return false;
            }
            commands.push_back(barrier->getHandle());
        }

        commandBuffers.insert(commandBuffers.end(), commands.begin(), commands.end());
        const FPresentSync surfaceSync = presentSyncOf(*frame.surface, imageIndex);
        sync.waits.insert(sync.waits.end(), surfaceSync.waits.begin(), surfaceSync.waits.end());
        sync.signals.insert(sync.signals.end(), surfaceSync.signals.begin(), surfaceSync.signals.end());
        presents.push_back(Present{
            .surface    = frame.surface,
            .imageIndex = imageIndex,
        });
        return true;
    }

    /// One `submitFrame` for every contributed surface, then `present` each.
    /// No contributed surface is success and does not submit.
    [[nodiscard]] bool submitAndPresent(IRender& render)
    {
        if (presents.empty()) {
            return true;
        }
        if (!render.submitFrame(commandBuffers, sync.waits, sync.signals)) {
            return false;
        }
        bool bPresented = true;
        for (const Present& present : presents) {
            bPresented = present.surface->present(present.imageIndex) && bPresented;
        }
        commandBuffers.clear();
        sync = {};
        presents.clear();
        return bPresented;
    }
};

/// Submit `commandBuffers` as the work that fills the acquired image, then
/// present it. An empty command list still legalizes the image (see
/// `presentFallbackCommand`), and `imageIndex < 0` is a no-op for both steps.
///
/// One surface. A frame that presents several windows adds each acquired image
/// to one `FFrameSubmission` and calls `submitAndPresent` once.
inline bool submitPresentFrame(IRender& render, FPresentFrame& frame, std::vector<void*> commandBuffers)
{
    FFrameSubmission submission;
    if (!submission.add(frame, std::move(commandBuffers))) {
        return false;
    }
    return submission.submitAndPresent(render);
}

} // namespace ya
