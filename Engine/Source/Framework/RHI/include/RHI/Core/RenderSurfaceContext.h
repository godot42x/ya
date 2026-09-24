#pragma once

#include "Core/Api.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace ya
{

struct INativeWindow;
struct ISwapchain;
struct IRenderResourceFactory;
struct RenderTexture;
struct ICommandBuffer;

/// One OS-window present destination on a shared `IRender` device: native
/// window, swapchain, acquire/present sync.
///
/// This is not a viewport. `begin()` chooses which swapchain image this
/// window will compose onto and present; actual rendering targets a
/// separate offscreen `RenderTexture`. A device keeps every one of these in
/// one registry and privileges none of them: the windows it was created for
/// are registered while it is created (see `RenderCreateInfo::startupSurfaces`)
/// and later ones through `IRender::createSurfaceContext`, and both are
/// addressed by `SurfaceId`. No surface owns frame-level state -- see
/// `IRender::beginRecordedFrame` and `recordedFrameIndex`.
struct YA_RHI_API IRenderSurfaceContext
{
    IRenderSurfaceContext() = default;
    virtual ~IRenderSurfaceContext() = default;

    IRenderSurfaceContext(const IRenderSurfaceContext&)            = delete;
    IRenderSurfaceContext& operator=(const IRenderSurfaceContext&) = delete;
    IRenderSurfaceContext(IRenderSurfaceContext&&)                 = delete;
    IRenderSurfaceContext& operator=(IRenderSurfaceContext&&)      = delete;

    [[nodiscard]] virtual INativeWindow* getNativeWindow() const = 0;
    [[nodiscard]] virtual ISwapchain*    getSwapchain() const    = 0;

    virtual bool buildPresentationImages(
        IRenderResourceFactory& factory,
        const char* labelPrefix,
        std::vector<std::shared_ptr<RenderTexture>>& outImages) = 0;

    /// Extent > 0 and the native window is not minimized. Unpresentable is
    /// not a paused process: skip this surface's acquire/present only.
    [[nodiscard]] virtual bool isPresentable() const = 0;

    virtual void requestRecreate() = 0;

    /// Wait this surface's in-flight fence, apply dirty recreate, acquire.
    /// `*imageIndex == -1` means unpresentable / no image; skip present.
    /// The frame's own completion is the device's (`IRender::beginRecordedFrame`
    /// waits it), so this only does what is specific to THIS window.
    virtual bool begin(int32_t* imageIndex) = 0;

    /// A command buffer that makes `imageIndex` legal to present on its own, for
    /// the frame that has nothing to fill the image with (a refused recording, a
    /// surface whose content is empty this tick). It records the layout
    /// transition to this surface's present layout and nothing else.
    ///
    /// It is the surface's because only the surface knows its acquired
    /// swapchain image and the layout present requires -- and it is a COMMAND,
    /// not a submission: the application submits it with every other surface's
    /// work through `IRender::submitFrame`. Null when the surface cannot provide
    /// one (an inactive or headless surface).
    [[nodiscard]] virtual ICommandBuffer* presentFallbackCommand(uint32_t imageIndex) = 0;

    /// Present the acquired image, after the frame submitted the work that fills
    /// it (`IRender::submitFrame`). This is the only step that advances this
    /// surface's acquire slot and the only one that touches this swapchain's
    /// presentation.
    virtual bool present(int32_t imageIndex) = 0;

    /// Wait until this window's last presented image is fully in use no longer:
    /// its present-complete fences (and, before a recreate, the swapchain's
    /// pending state). Does not `vkDeviceWaitIdle` and does not drain other
    /// windows' queues. Call at a frame boundary -- before destroying the
    /// context, after `present()`, or before swapchain recreate -- never between
    /// `begin()` and `present()` for the same acquired image.
    virtual void waitInFlight() = 0;

    [[nodiscard]] virtual void*    getCurrentImageAvailableSemaphore() = 0;
    [[nodiscard]] virtual void*    getRenderFinishedSemaphore(uint32_t imageIndex) = 0;
    // The pair above is what a window contributes to a submission, which is why
    // the surface's `submit` is deliberately absent -- submission is a queue
    // operation over command buffers, and `IRender::submitFrame` is where the
    // frame makes one. A frame that presents several windows therefore builds
    // one submission from every acquired surface's pair, instead of each window
    // submitting for itself.
    // `getCurrentFrameFence()` used to be here: it handed the caller a fence
    // that belonged to the FRAME but was stored on the window, which made one
    // window the owner of "did this frame's GPU work finish" and left a frame
    // that presented nothing with no fence at all. The device owns that fence
    // now (`IRender::beginRecordedFrame` / `IRender::submitFrame`).
    // `getCurrentFrameIndex()` used to be here: the surface's own acquire-slot
    // counter exposed so callers could ask a WINDOW which frame it was on (the
    // world recording's flight slot and the GPU-timing ring both did). Both now
    // key off `IRender::recordedFrameIndex()`, which is the frame's own number;
    // the slot stays private to the surface, where it only picks this
    // surface's semaphores and fence.
};

} // namespace ya
