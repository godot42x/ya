#pragma once

#include "Render/Render.h"
#include "Runtime/Rendering/Common/IRenderPipeline.h"
#include "Runtime/Rendering/Common/RenderOverlay.h"

namespace ya
{

struct App;
struct Entity;
struct RenderRuntime;

class AppFrameLoop
{
  public:
    static int      run(App& app);
    static int      iterate(App& app, float dt);
    static void     tickLogic(App& app, float dt);
    static void     syncViewportState(App& app);
    static Extent2D resolveViewportExtent(const App& app, RenderRuntime* renderRuntime, const Rect2D& viewportRect);
    static Entity*  getPrimaryCamera(const App& app);
    static void     prepareRenderFrameState(App& app, float dt);
    static void     tickRender(App& app, float dt);
    static uint32_t resolveFlightIndex(const App& app);

  private:
    static std::vector<RenderOverlaySprite2D> buildScreenOverlaySprites(const App& app);
    static std::vector<RenderOverlaySprite3D> buildWorldOverlaySprites(const App& app,
                                                                       Scene* scene,
                                                                       const RenderPipelineFrameContext& pipelineFrame);
};

} // namespace ya
