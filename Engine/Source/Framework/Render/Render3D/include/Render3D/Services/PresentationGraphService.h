#pragma once

#include "Core/Base.h"

#include "Core/Common/Types.h"
#include "Graph/RenderGraph.h"
#include "RHI/RenderDefines.h"
#include "Render3D/Common/FrameRecordExtensions.h"
#include "Render3D/Common/SurfaceImage.h"

#include <memory>
#include <vector>

namespace ya
{

struct ICommandBuffer;
struct IRender;
struct IRenderSurfaceContext;
struct ISwapchain;
class RenderSubmission;
class RenderGraphExecutor;
struct RenderTexture;
struct ISurfaceBackdropWriter;

/**
 * Owns the presentation graph: per-swapchain-image executors + imported
 * presentation images and the one pass that puts the host's content onto that
 * surface's swapchain image.
 *
 * The surface pass writes a finished image and nothing else: no state, no
 * grading, no postprocess pipeline. It samples the image it is handed and
 * stretches it to the surface's extent, so the only thing it can get wrong is
 * the pairing of what it is handed with the surface it writes into -- which is
 * checked (see `findSurfaceImageMismatch`) instead of assumed. Everything that
 * decides how an image looks happens where the image is produced.
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
        IRender*                  render  = nullptr;
        IRenderSurfaceContext*    present = nullptr;
        /// Draws the backdrop image onto the surface when the host declares the
        /// window shows it. Null is a real answer: the surface then holds only
        /// this pass's clear, which is what a host whose own content fills the
        /// window wants. The writer is a renderer-side pass, so nothing that
        /// processes an image lives in this service.
        ISurfaceBackdropWriter* backdropWriter = nullptr;
    };

    void init(const InitDesc& desc);
    void shutdown();

    /// Rebuild this surface's imported swapchain images + executors.
    /// Called on init and on THIS swapchain's `onRecreate` only.
    void rebuildImages();

    /// The surface pass for one acquired swapchain image. Not view compose; does
    /// not write Camera RTs.
    ///
    /// `backdrop` is the ready image this frame's surface starts from, or an
    /// invalid one when the host's own passes (recorded through `extensions`)
    /// are the whole of the window -- then the surface keeps this pass's clear.
    /// The pass itself runs either way, so the record order does not depend on
    /// which of the two this frame is.
    ///
    /// A `backdrop` whose encoding does not survive this surface's format is
    /// refused with an error rather than written: the hardware would apply the
    /// transfer function a second time.
    ///
    /// `extensions` is the host's contribution to this stage; null when the
    /// host records nothing here. The stage order is owned here and in the
    /// coordinator, not by the caller.
    void recordDisplayCompose(const FSurfaceImage&     backdrop,
                              RenderSubmission&        submission,
                              float                    deltaTime,
                              IFrameRecordExtensions*  extensions,
                              ICommandBuffer*          cmdBuf);

    [[nodiscard]] std::shared_ptr<RenderTexture> getCurrentPresentationImageShared() const;
    [[nodiscard]] uint32_t                     getCurrentPresentationImageIndex() const;
    [[nodiscard]] ISwapchain*                  getSwapchain() const;

  private:
    IRender*               _render  = nullptr;
    IRenderSurfaceContext* _present = nullptr;
    std::vector<std::unique_ptr<RenderGraphExecutor>> _presentationGraphExecutors;
    std::vector<std::shared_ptr<RenderTexture>>       _presentationImages;
    ISurfaceBackdropWriter*                           _backdropWriter = nullptr;
};

} // namespace ya
