#pragma once

#include "Core/Api.h"

#include <cstdint>
#include <vector>

namespace ya
{

struct INativeWindow;
struct ISwapchain;

/// One presentation surface on a shared `IRender` device: native window,
/// swapchain, acquire/submit/present sync. Extra windows use this instead of
/// `IRender::begin/end` / `getSwapchain()`. The primary window keeps the
/// existing `IRender` facade.
struct YA_RHI_API IRenderSurfaceContext
{
    IRenderSurfaceContext() = default;
    virtual ~IRenderSurfaceContext() = default;

    IRenderSurfaceContext(const IRenderSurfaceContext&)            = delete;
    IRenderSurfaceContext& operator=(const IRenderSurfaceContext&) = delete;
    IRenderSurfaceContext(IRenderSurfaceContext&&)                 = delete;
    IRenderSurfaceContext& operator=(IRenderSurfaceContext&&)      = delete;

    [[nodiscard]] virtual INativeWindow* getNativeWindow() const = 0;
    [[nodiscard]] virtual ISwapchain*    getSwapchain()          = 0;

    /// Wait this surface's in-flight fence, apply dirty recreate, acquire.
    /// `*imageIndex == -1` means minimized / no image; skip record+end.
    virtual bool begin(int32_t* imageIndex) = 0;

    /// Submit `commandBuffers` (wait image-available, signal render-finished)
    /// then present this swapchain. Empty commandBuffers still submit a
    /// present-layout barrier so the acquired image is legal to present.
    virtual bool end(int32_t imageIndex, std::vector<void*> commandBuffers) = 0;

    [[nodiscard]] virtual void*    getCurrentImageAvailableSemaphore() = 0;
    [[nodiscard]] virtual void*    getCurrentFrameFence()              = 0;
    [[nodiscard]] virtual void*    getRenderFinishedSemaphore(uint32_t imageIndex) = 0;
    [[nodiscard]] virtual uint32_t getCurrentFrameIndex() const        = 0;
};

} // namespace ya
