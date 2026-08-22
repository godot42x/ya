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
class RenderGraphExecutor;
struct RenderTexture;
struct BasicPostprocessing;

/**
 * Owns the presentation graph: per-swapchain-image executors + imported
 * presentation images and the post-process pipeline that composits the final
 * viewport image into the swapchain target.
 *
 * Presentation resources are intentionally independent from the world-frame
 * executor: swapchain acquire/present and recreate stay outside the world
 * graph (FG-601/602/603). The service only consumes the viewport display
 * image provided through InitDesc; it owns no world/UI state.
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
        IRender* render = nullptr;
        /// Supplies the final viewport display image (postprocessed output or
        /// raw viewport image) that the presentation graph composits.
        std::function<std::shared_ptr<RenderTexture>()> viewportDisplayImageProvider;
    };

    void init(const InitDesc& desc);
    void shutdown();

    /// Rebuild per-swapchain-image presentation executors + imported images
    /// (called on init and on swapchain recreate).
    void rebuildImages();

    /// Record the presentation graph for the current swapchain image.
    void render(float deltaTime, const Extensions& extensions, ICommandBuffer* cmdBuf);

    [[nodiscard]] std::shared_ptr<RenderTexture> getCurrentPresentationImageShared() const;
    [[nodiscard]] uint32_t                     getCurrentPresentationImageIndex() const;

  private:
    IRender* _render = nullptr;
    std::function<std::shared_ptr<RenderTexture>()> _viewportDisplayImageProvider;
    std::vector<std::unique_ptr<RenderGraphExecutor>> _presentationGraphExecutors;
    std::vector<std::shared_ptr<RenderTexture>>       _presentationImages;
    stdptr<BasicPostprocessing>                       _presentationPostProcessor = nullptr;
    PostProcessingState                               _presentationPostProcessState{};
};

} // namespace ya