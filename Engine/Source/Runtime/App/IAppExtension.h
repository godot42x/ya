#pragma once

#include "Core/Event.h"

#include <SDL3/SDL_events.h>

namespace ya
{

struct App;
struct ICommandBuffer;
struct Scene;

// Optional host capability. Runtime owns only this generic lifecycle contract.
struct IAppExtension
{
    virtual ~IAppExtension() = default;

    virtual void onAttach(App& app) {}
    virtual void onDetach(App& app) {}
    virtual void onSceneActivated(App& app, Scene* scene) {}
    virtual void onSceneDestroyed(App& app, Scene* scene) {}
    virtual void onNativeEvent(App& app, const SDL_Event& event) {}
    virtual bool onEvent(App& app, const Event& event) { return false; }
    virtual void onLogic(App& app, float dt) {}
    virtual void onBeforeRender(App& app, float dt) {}
    virtual void onPresentation(App& app, ICommandBuffer& commandBuffer, float dt) {}
};

} // namespace ya
