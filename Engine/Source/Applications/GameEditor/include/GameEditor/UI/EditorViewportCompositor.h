#pragma once

#include "Core/Common/Types.h"

#include <memory>
#include <string>

namespace ya
{

struct HostViewState;
struct EditorLayer;
struct ICommandBuffer;
struct IImage;
struct IImageView;
struct IRender;
struct RenderTexture;
struct RenderViewportSnapshot;
struct Texture;

/// Offscreen 2D compose of the authoring viewport: world color + overlays, or
/// the 2D canvas preview. Output is sampled by chrome `UIImage`; this object
/// does not present to the swapchain.
class EditorViewportCompositor
{
    std::shared_ptr<RenderTexture> _composedViewportImage;
    std::shared_ptr<Texture>       _sourceViewportTexture;
    std::shared_ptr<IImage>        _sourceViewportImage;
    std::shared_ptr<IImageView>    _sourceViewportImageView;
    std::string                    _scenePreviewErrors;

  public:
    void shutdown();
    [[nodiscard]] std::shared_ptr<RenderTexture> getOutputImage() const
    {
        return _composedViewportImage;
    }

    void compose(IRender&                      render,
                 ICommandBuffer&               commandBuffer,
                 const RenderViewportSnapshot& snapshot,
                 EditorLayer&                  layer,
                 const HostViewState&          hostView,
                 const Extent2D&               canvasTargetExtent);

  private:
    void composeCanvasPreview(IRender&        render,
                              ICommandBuffer& commandBuffer,
                              EditorLayer&    layer,
                              const Extent2D& canvasTargetExtent);
    void composeWorldFallback(IRender&                   render,
                              ICommandBuffer&            commandBuffer,
                              EditorLayer&               layer,
                              const HostViewState&       hostView,
                              const Extent2D&            canvasTargetExtent);
    void composeWorldFromScene(IRender&                      render,
                               ICommandBuffer&               commandBuffer,
                               const RenderViewportSnapshot& snapshot,
                               EditorLayer&                  layer,
                               const HostViewState&          hostView);
    std::shared_ptr<Texture> resolveSourceTexture(const RenderTexture& source);
    void ensureTarget(IRender& render, const RenderTexture& source);
    void ensureCanvasTarget(IRender& render, const Extent2D& extent);
};

} // namespace ya
