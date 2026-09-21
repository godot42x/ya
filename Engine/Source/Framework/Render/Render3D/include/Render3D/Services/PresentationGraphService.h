#pragma once

#include "Core/Base.h"

#include "Core/Common/Types.h"
#include "Graph/RenderGraph.h"
#include "RHI/Core/DescriptorSet.h"
#include "RHI/RenderDefines.h"
#include "Render3D/Common/FrameRecordExtensions.h"
#include "Render3D/Common/PostProcessingState.h"
#include "Render3D/Common/ViewPassResources.h"

#include <functional>
#include <memory>
#include <vector>

namespace ya
{

struct ICommandBuffer;
struct IRender;
struct IRenderSurfaceContext;
struct ISwapchain;
class RenderGraphExecutor;
struct RenderTexture;
struct BasicPostprocessing;

/**
 * Owns the presentation graph: per-swapchain-image executors + imported
 * presentation images and the display-copy pipeline that puts the host
 * window's content onto that surface's swapchain target.
 *
 * The injected `IRenderSurfaceContext` is a present destination only. World
 * rendering stays on offscreen viewport textures; this service never sizes
 * or formats the viewport from the swapchain. `onRecreate` rebuilds only
 * THIS surface's imported images (FG-601/602/603).
 */
struct YA_RENDER_3D_API PresentationGraphService
{
    struct InitDesc
    {
        IRender*                render  = nullptr;
        IRenderSurfaceContext*  present = nullptr;
        /// Supplies the View display image this service copies onto the
        /// acquired swapchain image when the host declares the surface shows
        /// that View (see `PresentFrameInput::bCopyViewDisplayImage`). Never
        /// called on a frame whose host declares it fills the surface itself.
        std::function<std::shared_ptr<RenderTexture>()> viewDisplayImageProvider;
    };

    void init(const InitDesc& desc);
    void shutdown();

    /// Rebuild this surface's imported swapchain images + executors.
    /// Called on init and on THIS swapchain's `onRecreate` only.
    void rebuildImages();

    /// Display compose for one acquired swapchain image. Not view compose; does
    /// not write Camera RTs.
    ///
    /// `bCopyViewDisplayImage` is the host's answer to "what does the window
    /// show": true copies the View's display image across the surface, false
    /// leaves the surface holding this pass's own clear so the host's own
    /// passes (recorded through `extensions`) are the whole of the window. The
    /// pass itself runs either way.
    ///
    /// `extensions` is the host's contribution to this stage; null when the
    /// host records nothing here. The stage order is owned here and in the
    /// coordinator, not by the caller.
    void recordDisplayCompose(bool                     bCopyViewDisplayImage,
                              float                    deltaTime,
                              IFrameRecordExtensions*  extensions,
                              ICommandBuffer*          cmdBuf);

    [[nodiscard]] std::shared_ptr<RenderTexture> getCurrentPresentationImageShared() const;
    [[nodiscard]] uint32_t                     getCurrentPresentationImageIndex() const;
    [[nodiscard]] ISwapchain*                  getSwapchain() const;

  private:
    IRender*               _render  = nullptr;
    IRenderSurfaceContext* _present = nullptr;
    std::function<std::shared_ptr<RenderTexture>()> _viewDisplayImageProvider;
    std::vector<std::unique_ptr<RenderGraphExecutor>> _presentationGraphExecutors;
    std::vector<std::shared_ptr<RenderTexture>>       _presentationImages;
    // Copies a View display image onto the swapchain. Runs in the pass-through
    // configuration (see kDisplayComposeState): the View's own finalize already
    // graded and encoded the pixels, so this is a stretch-and-sample copy.
    stdptr<BasicPostprocessing>                       _displayImageCopy = nullptr;
    // Surface-owned input set for that copy. View tone-map sets live on
    // ViewResources; this pass is display compose, not a View.
    stdptr<IDescriptorPool>                           _presentationInputPool;
    ToneMapPassBindings                               _displayImageCopyBindings{};
};

} // namespace ya
