#pragma once

#include "Core/Api.h"

#include <cstdint>
#include <functional>
#include <string>

namespace ya
{

struct InputManager;
struct Scene;
struct Entity;

/// Narrow services the world-script runtime may use. Injected by the host.
/// The runtime never reaches Host or App types.
struct WorldScriptServices
{
    const InputManager*                                             input = nullptr;
    std::function<bool()>                                           isMouseCaptured;
    std::function<double()>                                         elapsedSeconds;
    std::function<uint64_t()>                                       frameIndex;
    std::function<Scene*()>                                         activeScene;
    std::function<bool(const std::string& path, std::string& out)> readScript;
};

/// What GameRuntime drives. The concrete runtime lives in the language
/// backend. The destructor is virtual so GameRuntime can `delete` this
/// pointer across the DLL boundary; the derived destructor runs in the
/// backend via the vtable.
struct YA_ECS_SYSTEMS_API IWorldScriptRuntime
{
    virtual ~IWorldScriptRuntime() = default;

    virtual void setServices(WorldScriptServices services) = 0;
    virtual void init()                                     = 0;
    virtual void shutdown()                                 = 0;
    virtual void onUpdate(float deltaTime)                  = 0;
    virtual void onStop()                                   = 0;
    virtual void onEntityDestroying(Entity& entity)         = 0;
    virtual void resetPersistentState()                     = 0;
    virtual void dropInstance(uint64_t runtimeId)           = 0;
};

using WorldScriptRuntimeFactory = IWorldScriptRuntime* (*)();

YA_ECS_SYSTEMS_API void                 setWorldScriptRuntimeFactory(WorldScriptRuntimeFactory factory);
YA_ECS_SYSTEMS_API IWorldScriptRuntime* createWorldScriptRuntime();

} // namespace ya
