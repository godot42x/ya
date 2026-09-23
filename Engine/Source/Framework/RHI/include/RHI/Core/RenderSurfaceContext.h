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
/// separate offscreen `RenderTexture`. The window created with the device
/// is `IRender::getPrimarySurfaceContext()`. Extra windows use
/// `IRender::createSurfaceContext`.
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
    /// `*imageIndex == -1` means unpresentable / no image; skip record+end.
    /// Does not reset the in-flight fence when skipping.
    virtual bool begin(int32_t* imageIndex) = 0;

    /// Submit `commandBuffers` (wait image-available, signal render-finished)
    /// then present this swapchain. Empty commandBuffers still submit a
    /// present-layout barrier so the acquired image is legal to present.
    virtual bool end(int32_t imageIndex, std::vector<void*> commandBuffers) = 0;

    /// Wait this surface's graphics fences and present-complete fences.
    /// Does not `vkDeviceWaitIdle` and does not drain other windows' queues.
    /// Call at a frame boundary (before destroying the context, after `end()`,
    /// or before swapchain recreate). Do not call after `begin()` has reset
    /// the current fence and before `end()`.
    virtual void waitInFlight() = 0;

    [[nodiscard]] virtual void*    getCurrentImageAvailableSemaphore() = 0;
    [[nodiscard]] virtual void*    getCurrentFrameFence()              = 0;
    [[nodiscard]] virtual void*    getRenderFinishedSemaphore(uint32_t imageIndex) = 0;
    // `getCurrentFrameIndex()` used to be here: the surface's own acquire-slot
    // counter exposed so callers could ask a WINDOW which frame it was on (the
    // world recording's flight slot and the GPU-timing ring both did). Both now
    // key off `IRender::recordedFrameIndex()`, which is the frame's own number;
    // the slot stays private to the surface, where it only picks this
    // surface's semaphores and fence.
};

} // namespace ya
