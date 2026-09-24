#pragma once

#include "Core/Base.h"

#include "Render3D/Pipelines/SurfaceWritePass.h"
#include "Render3D/Services/PresentationGraphService.h"
#include "RHI/Core/SurfaceId.h"

#include <memory>

namespace ya

{

struct ICommandBuffer;
struct IRender;
struct IRenderSurfaceContext;
struct IFrameRecordExtensions;
struct RenderTexture;
class RenderSubmission;
struct FSurfaceImage;

/// One OS-window present target, owned by the renderer.
///
/// Everything that exists only because *this* surface exists lives here: its
/// imported swapchain images with their per-image executors, and the pass that
/// writes a ready image into this surface's format. The last part is why this is
/// a type instead of two members on the renderer -- the write pass is built from
/// the surface's swapchain format, so a second window with its own format gets
/// its own pass instead of the primary window's.
///
/// The renderer creates one per `IRenderSurfaceContext` the first time a host
/// records for it, and destroys them with the device. Nothing here decides which
/// View is shown or when acquire/present happen: those are the application's.
struct YA_RENDER_3D_API SurfacePresentation
{
    struct InitDesc
    {
        IRender*               render  = nullptr;
        IRenderSurfaceContext* present = nullptr;
        /// Which surface the device registry calls this one. The presentation
        /// is keyed by it, not by `present`'s address: a window that is closed
        /// and reopened can land on the same address, and imported swapchain
        /// images must never be attributed to the second window.
        SurfaceId              id{};
    };

    SurfaceId                _id{};
    IRenderSurfaceContext*   _present = nullptr;
    PresentationGraphService _graph{};
    /// The postprocess family's write onto this surface, built from this
    /// surface's format. Owned here because the format is this surface's fact.
    stdptr<SurfaceWritePass> _writePass;

    void init(const InitDesc& desc);
    void shutdown();

    [[nodiscard]] SurfaceId              id() const { return _id; }
    [[nodiscard]] IRenderSurfaceContext* surface() const { return _present; }
    [[nodiscard]] ISwapchain*            swapchain() const { return _graph.getSwapchain(); }

    /// The surface pass for one acquired swapchain image of this surface. See
    /// `PresentationGraphService::recordDisplayCompose` for the contract.
    void recordDisplayCompose(const FSurfaceImage&    backdrop,
                              RenderSubmission&       submission,
                              float                   deltaTime,
                              IFrameRecordExtensions* extensions,
                              ICommandBuffer*         cmdBuf);

    /// This surface's image for the acquired index, or null before acquire.
    [[nodiscard]] std::shared_ptr<RenderTexture> currentImageShared() const;
};

} // namespace ya
