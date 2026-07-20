#pragma once

#include "Core/Event.h"
#include "Runtime/App/AppState.h"

#include <SDL3/SDL_events.h>

namespace ya
{

struct App;
struct AppDesc;
struct ICommandBuffer;
struct Scene;

// Optional host capability. Runtime owns only this generic lifecycle contract.
struct IAppExtension
{
    virtual ~IAppExtension() = default;

    virtual void onConfigure(App& app, AppDesc& desc) {}
    virtual void onAttach(App& app) {}
    virtual void onDetach(App& app) {}
    virtual bool onBeforeAppStateChange(App& app, AppState previousState, AppState nextState) { return true; }
    virtual void onAfterAppStateChange(App& app, AppState previousState, AppState currentState) {}
    virtual void onSceneActivated(App& app, Scene* scene) {}
    virtual void onSceneDestroyed(App& app, Scene* scene) {}
    virtual void onNativeEvent(App& app, const SDL_Event& event) {}
    virtual bool onEvent(App& app, const Event& event) { return false; }
    virtual void onLogic(App& app, float dt) {}
    virtual void onBeforeRender(App& app, float dt) {}
    virtual void onBeforePresentation(App& app, ICommandBuffer& commandBuffer, float dt) {}
    virtual void onPresentation(App& app, ICommandBuffer& commandBuffer, float dt) {}
};

} // namespace ya
