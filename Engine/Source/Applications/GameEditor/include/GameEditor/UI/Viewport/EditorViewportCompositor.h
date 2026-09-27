#pragma once

#include "Core/Common/Types.h"

#include "GUI/Compose/Render2DComposePass.h"
#include "Render3D/WorldDraw.h"

#include "GameEditor/UI/Viewport/EditorGameUIPreview.h"

#include <memory>
#include <string>

namespace ya
{

struct EditorLayer;
struct ICommandBuffer;
struct IImage;
struct IImageView;
struct IRender;
struct RenderTexture;
struct RenderViewportSnapshot;
struct Texture;

/// Camera matrices for world-space overlay lines. They stay on the editor
/// side of the compose pass; the GUI pass description does not carry them.
struct EditorComposeCamera
{
    glm::vec3 position{0.0f};
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::mat4 viewProjection{1.0f};
};

/// Offscreen 2D compose of the authoring viewport: world color + overlays, or
/// the 2D canvas preview. Output is sampled by chrome `UIImage`; this object
/// does not present to the swapchain.
///
/// The world paths take a camera, not "the View this app displays": composing
/// world content is a per-View question, so the caller names which View's
/// camera it wants composed -- the authoring viewport's today, any other
/// View's just as well.
class EditorViewportCompositor
{
    std::shared_ptr<RenderTexture> _composedViewportImage;
    std::shared_ptr<Texture>       _sourceViewportTexture;
    std::shared_ptr<IImage>        _sourceViewportImage;
    std::shared_ptr<IImageView>    _sourceViewportImageView;
    /// The current Scene's mounts, instantiated once and reused across frames.
    EditorGameUIPreview            _scenePreview;
    ScreenDrawPipelines*           _screenPipelines = nullptr;
    WorldDrawPipelines*            _worldPipelines  = nullptr;
    ScreenDrawRecorder             _viewportScreen;
    ScreenDrawRecorder             _canvasScreen;
    WorldDrawRecorder              _world;
    bool                           _bRecordersBound = false;
    EFormat::T                     _worldDepthFormat = EFormat::Undefined;

  public:
    void bindDraw(ScreenDrawPipelines& screen, WorldDrawPipelines& world);
    /// `bCanvas` prepares the canvas recorder. Otherwise the viewport screen
    /// recorder and the world recorder are prepared for `depthFormat`.
    void prepare(EFormat::T colorFormat, EFormat::T depthFormat, bool bCanvas);
    void shutdown();
    [[nodiscard]] std::shared_ptr<RenderTexture> getOutputImage() const
    {
        return _composedViewportImage;
    }

    void compose(IRender&                            render,
                 ICommandBuffer&                     commandBuffer,
                 const RenderViewportSnapshot&       snapshot,
                 EditorLayer&                        layer,
                 const EditorComposeCamera&          worldCamera,
                 const Extent2D&                     canvasTargetExtent);

  private:
    void composeCanvasPreview(IRender&        render,
                              ICommandBuffer& commandBuffer,
                              EditorLayer&    layer,
                              const Extent2D& canvasTargetExtent);
    void composeWorldFallback(IRender&                         render,
                              ICommandBuffer&                  commandBuffer,
                              EditorLayer&                     layer,
                              const EditorComposeCamera&          worldCamera,
                              const Extent2D&                  canvasTargetExtent);
    void composeWorldFromScene(IRender&                         render,
                               ICommandBuffer&                  commandBuffer,
                               const RenderViewportSnapshot&    snapshot,
                               EditorLayer&                     layer,
                               const EditorComposeCamera& worldCamera);
    std::shared_ptr<Texture> resolveSourceTexture(const RenderTexture& source);
    void ensureTarget(IRender& render, const RenderTexture& source);
    void ensureCanvasTarget(IRender& render, const Extent2D& extent);
};

} // namespace ya
