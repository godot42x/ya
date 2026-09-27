#pragma once

#include "GUI/Host/GUIPresentationTarget.h"
#include "Render2D/Render2D.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "RHI/Core/PresentFrame.h"

#include <cstdint>
#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

namespace ya
{

struct IRender;
struct IRenderSurfaceContext;
struct ICommandBuffer;
class GUIRenderSurface;
struct ISwapchain;

/// Per-surface display compose resources for one extra (or reusable) GUI window.
/// Does not own the `IRender` device.
struct FGUISurfacePresentResources
{
    IRender*               render  = nullptr;
    IRenderSurfaceContext* present = nullptr;
    std::vector<std::shared_ptr<ICommandBuffer>>       commandBuffers;
    std::vector<std::shared_ptr<GUIPresentationTarget>> presentationTargets;
    void*    cachedSwapchainHandle = nullptr;
    Extent2D cachedSwapchainExtent{};
};

void rebuildGuiSurfacePresentation(FGUISurfacePresentResources& resources,
                                   const char*                  labelPrefix,
                                   bool                         bWaitForGpu);

/// The recording facts a window's own present contributions run against: the
/// per-image command buffer is open, the surface's clear + UI compose are
/// already recorded, and end/submit have not run.
struct FGUIPresentExtensionContext
{
    ICommandBuffer&   cmdBuf;
    GUIRenderSurface& presentedSurface;
    ISwapchain&       swapchain;
    Extent2D          presentExtent;
};

/// Acquire and record this window's snapshot into `submission`. Does not
/// submit or present: the frame that owns the submission does that once, for
/// every window it acquired. Skips when unpresentable. Serial Render2D
/// session: caller must not be inside another `Render2D::begin`.
///
/// The one record sequence for every GUI window, main or extra. A window adds
/// its own content at two named points, both optional and both receiving
/// `FGUIPresentExtensionContext`:
/// - `composeExtra` records inside the surface's compose pass (overlays); it
///   receives the list the compose builds so overlays append to it;
/// - `preSubmit` records after the compose pass and before the command buffer
///   ends -- anything that must land in the same submission (readback copies).
void recordGuiSnapshot(FGUISurfacePresentResources&  resources,
                       const UIFrameSnapshot&        snapshot,
                       Extent2D                      logicalExtent,
                       Render2DPassSlot              passSlot,
                       bool                          bMinimized,
                       bool&                         bSwapchainRecreatePending,
                       FFrameSubmission&             submission,
                       const std::function<void(const FGUIPresentExtensionContext&, Render2DList&)>& composeExtra = {},
                       const std::function<void(const FGUIPresentExtensionContext&)>& preSubmit    = {});

[[nodiscard]] inline bool guiPresentationIndexValid(int32_t imageIndex,
                                                    size_t  targetCount,
                                                    size_t  commandBufferCount)
{
    return imageIndex >= 0 &&
           static_cast<size_t>(imageIndex) < targetCount &&
           static_cast<size_t>(imageIndex) < commandBufferCount;
}

} // namespace ya
