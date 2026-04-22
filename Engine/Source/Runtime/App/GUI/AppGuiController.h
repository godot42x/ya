#pragma once

namespace ya
{

struct App;

class AppGuiController
{
  public:
    static void onRenderGUI(App& app, float dt);
    static void renderGUI(App& app, float dt);
};

} // namespace ya
