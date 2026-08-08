#pragma once

#include "Core/Event.h"

namespace ya
{

struct App;

class AppEventRouter
{
  public:
    static int onEvent(App& app, const Event& event);

  private:
    static bool onWindowResized(App& app, const WindowResizeEvent& event);
    static bool onMouseMoved(App& app, const MouseMoveEvent& event);
};

} // namespace ya
