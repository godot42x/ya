#pragma once

#include "RHI/Render.h"
#include "Render3D/Common/IRenderPipeline.h"
#include "Render3D/Common/RenderOverlay.h"

namespace ya
{

struct App;
struct Entity;
struct RenderRuntime;

class AppFrameLoop
{
  public:
    static int      run(App& app);
    /// Run one product frame. Direct callers retain the legacy native event
    /// pump; AppKernel-backed run() passes false because its event source has
    /// already delivered the events for this frame.
    static int      iterate(App& app, float dt, bool bPumpNativeEvents = true);
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
