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
    static bool onKeyReleased(App& app, const KeyReleasedEvent& event);
    static bool onMouseMoved(App& app, const MouseMoveEvent& event);
    static bool onMouseButtonReleased(App& app, const MouseButtonReleasedEvent& event);
    static bool onMouseScrolled(App& app, const MouseScrolledEvent& event);
};

} // namespace ya
