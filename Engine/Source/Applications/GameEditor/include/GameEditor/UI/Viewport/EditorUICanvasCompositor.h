#pragma once

#include "Core/Common/Types.h"

#include "GUI/Compose/Render2DComposePass.h"

#include <memory>

namespace ya
{

struct EditorUIDesignerSession;
struct ICommandBuffer;
struct IRender;
struct RenderTexture;

/// Pictures the UI Designer's Canvas tab samples: the canvas grid, the open
/// document's preview tree under the canvas pan/zoom, and the selection
/// outline with its resize handles. Owns its offscreen target, sized from the
/// canvas tab. Records nothing while no canvas tab is on screen.
class EditorUICanvasCompositor
{
    std::shared_ptr<RenderTexture> _target;
    ScreenDrawRecorder             _screen;
    bool                           _bBound = false;

  public:
    void bindDraw(ScreenDrawPipelines& screen);
    /// Prepare the pipeline before command recording starts.
    void prepare();
    void shutdown();
    /// Record this frame's canvas and publish it into `designer.canvas().image`
    /// (null when no canvas is shown or it has no pixels).
    void compose(IRender& render, ICommandBuffer& commandBuffer, EditorUIDesignerSession& designer);

  private:
    void ensureTarget(IRender& render, const Extent2D& extent);
};

} // namespace ya
