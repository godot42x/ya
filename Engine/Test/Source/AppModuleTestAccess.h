#pragma once

#include "Host/App.h"
#include "Render3D/SceneManager.h"

namespace ya
{

struct JSScriptingSystem;

/**
 * @brief AppModuleTestAccess - friend-class test seam into App's private
 * members (App.h declares `friend class AppModuleTestAccess`).
 *
 * Shared by all engine tests that need to assemble a minimal App without
 * going through AppLifecycle::init (scene manager wiring, scripting system,
 * app state).
 */
class AppModuleTestAccess
{
  public:
    static void configure(App& app) { app.configureModules(); }
    static void attach(App& app) { app.attachModules(); }
    static void detach(App& app) { app.detachModules(); }
    static void setSceneManager(App& app, SceneManager* sceneManager) { app._sceneManager = sceneManager; }
    static void setAppState(App& app, AppState state) { app._appState = state; }
    static void setJSScriptingSystem(App& app, JSScriptingSystem* js) { app._jsScriptingSystem = js; }
    static bool dispatchEvent(App& app, const Event& event) { return app.dispatchHostModuleEvent(event); }
    static void tick(App& app, float dt) { app.tickModules(dt); }
    static void prepareRender(App& app, float dt) { app.prepareModulesForRender(dt); }
    static void recordPresentation(App& app, ICommandBuffer& commandBuffer, float dt)
    {
        app.recordModulePresentation(commandBuffer, dt);
    }
};

} // namespace ya
