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

/// One OS-window present destination on a shared `IRender` device: native
/// window, swapchain, acquire/submit/present sync.
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
    /// `*imageIndex == -1` means unpresentable / no image; skip submit+present.
    /// The frame's own completion is the device's (`IRender::beginRecordedFrame`
    /// waits it), so this only does what is specific to THIS window.
    virtual bool begin(int32_t* imageIndex) = 0;

    /// Submit the work that fills this surface's acquired image: wait
    /// image-available, signal render-finished. Empty `commandBuffers` still
    /// submits a present-layout barrier, so an acquired image is always legal to
    /// present. The fence is the DEVICE's frame fence (`IRender::submitFrame`),
    /// not this window's: several surfaces can be submitted in one frame and the
    /// frame is done when all of them are.
    virtual bool submit(int32_t imageIndex, std::vector<void*> commandBuffers) = 0;

    /// Present the acquired image, after this surface's `submit` for it. This is
    /// the only step that advances this surface's acquire slot and the only one
    /// that touches this swapchain's presentation.
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
