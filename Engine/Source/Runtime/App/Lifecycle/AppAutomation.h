#pragma once

namespace ya
{

struct App;
struct AppDesc;
struct AppAutomationOptions;
struct ICommandBuffer;

class AppAutomation
{
  public:
    static void beginRuntimeProfiling(const AppAutomationOptions& automation);
    static void endRuntimeProfiling();
    static void applyStartupOverrides(AppDesc& appDesc);
    static void applyRuntimeOverrides(App& app);
    static bool shouldDeferQuit(const App& app);
    static void recordPresentationCapture(App& app, ICommandBuffer* cmdBuf);
    static void onFrameCompleted(App& app);
};

} // namespace ya
