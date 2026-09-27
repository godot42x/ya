#pragma once

#include "Core/Common/Types.h"

#include "GUI/Compose/Render2DComposePass.h"
#include "Render3D/WorldDraw.h"

#include "GameEditor/UI/Viewport/EditorGameUIPreview.h"

#include <memory>

namespace ya
{

struct EditorLayer;
struct ICommandBuffer;
struct IRender;
struct RenderTexture;
struct RenderViewportSnapshot;

/// Camera matrices for world-space overlay lines. They stay in GameEditor;
/// the GUI compose description does not carry them.
struct EditorComposeCamera
{
    glm::vec3 position{0.0f};
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::mat4 viewProjection{1.0f};
};

/// Pictures the authoring viewport widget samples.
///
/// 3D draws into the View's tone-mapped display image: world lines with the
/// View depth (test, no write), then gizmo / HUD. 2D owns a canvas preview.
/// This object does not present to the swapchain.
class EditorViewportCompositor
{
    std::shared_ptr<RenderTexture> _canvasImage;
    std::shared_ptr<RenderTexture> _publishedOutput;
    EditorGameUIPreview            _scenePreview;
    ScreenDrawPipelines*           _screenPipelines = nullptr;
    WorldDrawPipelines*            _worldPipelines  = nullptr;
    ScreenDrawRecorder             _viewportScreen;
    ScreenDrawRecorder             _canvasScreen;
    WorldDrawRecorder              _world;
    bool                           _bRecordersBound    = false;
    EFormat::T                     _overlayColorFormat = EFormat::Undefined;
    EFormat::T                     _worldDepthFormat   = EFormat::Undefined;

  public:
    void bindDraw(ScreenDrawPipelines& screen, WorldDrawPipelines& world);
    /// `bCanvas` prepares the canvas recorder. Otherwise the viewport screen
    /// recorder and the world recorder are prepared for the display image and
    /// the View depth.
    void prepare(EFormat::T colorFormat, EFormat::T depthFormat, bool bCanvas);
    void shutdown();
    [[nodiscard]] std::shared_ptr<RenderTexture> getOutputImage() const
    {
        return _publishedOutput;
    }

    void compose(IRender&                      render,
                 ICommandBuffer&               commandBuffer,
                 const RenderViewportSnapshot& snapshot,
                 EditorLayer&                  layer,
                 const EditorComposeCamera&    worldCamera,
                 const Extent2D&               canvasTargetExtent);

  private:
    void composeCanvasPreview(IRender&        render,
                              ICommandBuffer& commandBuffer,
                              EditorLayer&    layer,
                              const Extent2D& canvasTargetExtent);
    void composeAuthoringView(ICommandBuffer&            commandBuffer,
                              const RenderViewportSnapshot& snapshot,
                              EditorLayer&               layer,
                              const EditorComposeCamera& worldCamera);
    void recordViewOverlay(ICommandBuffer&            commandBuffer,
                           RenderTexture&             color,
                           RenderTexture&             depth,
                           EditorLayer&               layer,
                           const EditorComposeCamera& worldCamera);
    void ensureCanvasTarget(IRender& render, const Extent2D& extent);
};

} // namespace ya
