#pragma once

#include "Render/Render.h"

#include <SDL3/SDL_events.h>

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
    static int      processEvent(App& app, SDL_Event& event);
    static void     tickLogic(App& app, float dt);
    static void     syncViewportState(App& app);
    static Extent2D resolveViewportExtent(const App& app, RenderRuntime* renderRuntime, const Rect2D& viewportRect);
    static Entity*  getPrimaryCamera(const App& app);
    static void     prepareRenderFrameState(App& app, float dt);
    static void     tickRender(App& app, float dt);
    static uint32_t resolveFlightIndex(const App& app);
};

} // namespace ya
