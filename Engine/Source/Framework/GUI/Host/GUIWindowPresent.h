#pragma once

#include "GUI/Host/GUIPresentationTarget.h"
#include "Render2D/Render2D.h"
#include "GUI/Widgets/UIFrameSnapshot.h"

#include <memory>
#include <vector>

namespace ya
{

struct IRender;
struct IRenderSurfaceContext;
struct ICommandBuffer;

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

/// Acquire, compose `snapshot` onto this surface, present. Skips when
/// unpresentable. Serial Render2D session: caller must not be inside another
/// `Render2D::begin`.
void presentGuiSnapshot(FGUISurfacePresentResources& resources,
                        const UIFrameSnapshot&       snapshot,
                        Extent2D                     logicalExtent,
                        Render2DPassSlot             passSlot,
                        bool                         bMinimized,
                        bool&                        bSwapchainRecreatePending);

} // namespace ya
