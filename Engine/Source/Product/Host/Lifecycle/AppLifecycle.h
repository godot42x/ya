#pragma once

#include "Foundation/Core/Api.h"

#include <string>

namespace ya
{

struct App;
struct AppDesc;
struct Scene;

class YA_HOST_API AppLifecycle
{
  public:
    static void init(App& app, AppDesc ci);
    static void handleSystemSignals(App& app);
    static void onInit(App& app, const AppDesc& ci);
    static void onPostInit(App& app);
    static void quit(App& app);

    static std::string resolveStartupScenePath(const AppDesc& appDesc);
    static bool loadScene(App& app, const std::string& path);
    static bool unloadScene(App& app);

    static void onSceneInit(App& app, Scene* scene);
    static void onSceneDestroy(App& app, Scene* scene);
    static void onSceneActivated(App& app, Scene* scene);

    static void onEnterRuntime(App& app);
    static void startRuntime(App& app);
    static void startSimulation(App& app);
    static void stopRuntime(App& app);
    static void stopSimulation(App& app);
};

} // namespace ya
