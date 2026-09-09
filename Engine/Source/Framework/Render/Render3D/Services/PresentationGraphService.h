#pragma once

#include "Core/Base.h"

#include "Core/Common/Types.h"
#include "Graph/RenderGraph.h"
#include "RHI/RenderDefines.h"
#include "Render3D/Common/PostProcessingState.h"

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
 * presentation images and the post-process pipeline that composits the final
 * viewport image into that surface's swapchain target.
 *
 * The injected `IRenderSurfaceContext` is a present destination only. World
 * rendering stays on offscreen viewport textures; this service never sizes
 * or formats the viewport from the swapchain. `onRecreate` rebuilds only
 * THIS surface's imported images (FG-601/602/603).
 */
struct YA_RENDER_3D_API PresentationGraphService
{
    /// Presentation graph extension points recorded by the app. A single
    /// descriptor object keeps the presentation boundary explicit instead of
    /// threading several parallel callbacks through FrameInput.
    struct Extensions
    {
        std::function<void(ICommandBuffer*)>                         recordBeforeExtensions;
        std::function<void(ICommandBuffer*)>                         recordExtensions;
        std::function<bool(RenderGraph&, RGTextureHandle, Extent2D)> appendCapture;

        [[nodiscard]] bool empty() const
        {
            return !recordBeforeExtensions && !recordExtensions && !appendCapture;
        }
    };

    struct InitDesc
    {
        IRender*                render  = nullptr;
        IRenderSurfaceContext*  present = nullptr;
        /// Supplies the final viewport display image (postprocessed output or
        /// raw viewport image) that the presentation graph composits onto the
        /// acquired swapchain image.
        std::function<std::shared_ptr<RenderTexture>()> viewportDisplayImageProvider;
    };

    void init(const InitDesc& desc);
    void shutdown();

    /// Rebuild this surface's imported swapchain images + executors.
    /// Called on init and on THIS swapchain's `onRecreate` only.
    void rebuildImages();

    /// Display compose: blit the Camera display RT onto this surface's
    /// `swapchain[imageIndex]`. Not view compose; does not write Camera RTs.
    void recordDisplayCompose(float deltaTime, const Extensions& extensions, ICommandBuffer* cmdBuf);

    [[nodiscard]] std::shared_ptr<RenderTexture> getCurrentPresentationImageShared() const;
    [[nodiscard]] uint32_t                     getCurrentPresentationImageIndex() const;
    [[nodiscard]] ISwapchain*                  getSwapchain() const;

  private:
    IRender*               _render  = nullptr;
    IRenderSurfaceContext* _present = nullptr;
    std::function<std::shared_ptr<RenderTexture>()> _viewportDisplayImageProvider;
    std::vector<std::unique_ptr<RenderGraphExecutor>> _presentationGraphExecutors;
    std::vector<std::shared_ptr<RenderTexture>>       _presentationImages;
    stdptr<BasicPostprocessing>                       _presentationPostProcessor = nullptr;
    PostProcessingState                               _presentationPostProcessState{};
};

} // namespace ya