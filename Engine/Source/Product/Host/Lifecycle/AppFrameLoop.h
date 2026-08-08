#pragma once

#include "Foundation/RHI/Render.h"
#include "Framework/Game/Render/Render3D/Common/IRenderPipeline.h"
#include "Framework/Game/Render/Render3D/Common/RenderOverlay.h"

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
};

} // namespace ya
