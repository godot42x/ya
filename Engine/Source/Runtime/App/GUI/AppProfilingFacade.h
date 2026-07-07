#pragma once

namespace ya
{

struct App;

class AppProfilingFacade
{
  public:
    static void render(App& app);
    static void renderContents(App& app);
};

} // namespace ya
