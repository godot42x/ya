#pragma once

namespace ya
{

struct App;
struct AppDesc;

class AppAutomation
{
  public:
    static void applyStartupOverrides(AppDesc& appDesc);
    static bool shouldDeferQuit(const App& app);
    static void onFrameCompleted(App& app);
};

} // namespace ya
