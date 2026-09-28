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
/// Draws into the View's tone-mapped display image: mounted Game UI while
/// authoring, then world lines with the View depth (test, no write), then
/// gizmo / HUD. This object does not present to the swapchain. The UI
/// Designer canvas is EditorUICanvasCompositor, not a mode of this one.
class EditorViewportCompositor
{
    std::shared_ptr<RenderTexture> _publishedOutput;
    EditorGameUIPreview            _scenePreview;
    ScreenDrawPipelines*           _screenPipelines = nullptr;
    WorldDrawPipelines*            _worldPipelines  = nullptr;
    ScreenDrawRecorder             _viewportScreen;
    /// Game UI overlay on the 3D display image. Separate from `_viewportScreen`
    /// because one recorder resets its flight buffer at the start of a record.
    ScreenDrawRecorder             _gameUiScreen;
    WorldDrawRecorder              _world;
    bool                           _bRecordersBound    = false;
    EFormat::T                     _overlayColorFormat = EFormat::Undefined;
    EFormat::T                     _worldDepthFormat   = EFormat::Undefined;

  public:
    void bindDraw(ScreenDrawPipelines& screen, WorldDrawPipelines& world);
    /// Prepare the screen and world recorders for the display image and the
    /// View depth.
    void prepare(EFormat::T colorFormat, EFormat::T depthFormat);
    void shutdown();
    [[nodiscard]] std::shared_ptr<RenderTexture> getOutputImage() const
    {
        return _publishedOutput;
    }

    void compose(ICommandBuffer&               commandBuffer,
                 const RenderViewportSnapshot& snapshot,
                 EditorLayer&                  layer,
                 const EditorComposeCamera&    worldCamera);

  private:
    void composeMountedGameUI(ICommandBuffer& commandBuffer,
                              RenderTexture&  color,
                              EditorLayer&    layer);
    void recordViewOverlay(ICommandBuffer&            commandBuffer,
                           RenderTexture&             color,
                           RenderTexture&             depth,
                           EditorLayer&               layer,
                           const EditorComposeCamera& worldCamera);
};

} // namespace ya
