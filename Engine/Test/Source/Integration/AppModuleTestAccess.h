#pragma once

#include "GameRuntime/App.h"
#include "GameRuntime/Lifecycle/GameRuntimeTickOrchestrator.h"
#include "Scene/Runtime/SceneManager.h"

namespace ya
{

struct JSScriptingSystem;
class AppAutomationControlService;

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
    static void setLuaScriptingSystem(App& app, LuaScriptingSystem* lua) { app._luaScriptingSystem = lua; }
    static void addSystem(App& app, stdptr<ISystem> system, ESystemTickGroup group)
    {
        app._systems.emplace_back(std::move(system), group);
    }
    static void clearSystems(App& app) { app._systems.clear(); }
    static void setGameUIHost(App& app, std::unique_ptr<GameUIHost> host) { app._gameUIHost = std::move(host); }
    static void tickLogic(App& app, float dt) { GameRuntimeTickOrchestrator::tickLogic(app, dt); }
    static AppAutomationControlService* getAutomationControlService(App& app) { return app.getAutomationControlService(); }
    static bool dispatchEvent(App& app, const Event& event) { return app.dispatchModuleEvent(event); }
    static void tick(App& app, float dt) { app.tickModules(dt); }
    static void prepareRender(App& app, float dt) { app.prepareModulesForRender(dt); }
    static void recordPresentation(App& app, ICommandBuffer& commandBuffer, float dt)
    {
        app.recordDisplayExtensions(commandBuffer, dt);
    }
    static void recordExtraSurfaces(App& app, float dt, FFrameSubmission& submission)
    {
        app.recordModuleExtraSurfaces(dt, submission);
    }
    static std::string resolveStartupScenePath(const AppDesc& desc)
    {
        return App::resolveStartupScenePath(desc);
    }
    static bool loadScene(App& app, const std::string& path)
    {
        return app.loadSceneInternal(path);
    }
    static bool loadSceneKeepingRunMode(App& app, const std::string& path)
    {
        return app.loadSceneKeepingRunMode(path);
    }
};

} // namespace ya
