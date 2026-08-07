#pragma once

#include "Core/Base.h"
#include "Core/Delegate.h"
#include "Core/System/System.h"
#include "Host/AppState.h"

#include <functional>
#include <memory>

#include "entt/entt.hpp"

namespace ya
{

struct Scene;
struct SceneManager;

/**
 * @brief PhysicsSystem - Minimal Jolt physics integration with the ECS.
 *
 * Every entity carrying a TransformComponent + PhysicsBodyComponent gets a
 * Jolt rigid body. The world is stepped with a fixed 60 Hz timestep and the
 * resulting body positions / rotations are written back into the entity
 * transforms every frame.
 *
 * All Jolt state lives behind the World pimpl so this header (and every TU
 * that includes it) stays free of Jolt headers and its global allocator hooks.
 */
struct YA_PHYSICS_API PhysicsSystem : public ISystem
{
  public:
    struct World;

  private:
    std::unique_ptr<World> _world;

    MulticastDelegate<void(AppState)>* _appStateChangedSource = nullptr;
    SceneManager*                      _sceneManager         = nullptr;
    // The scene whose bodies currently live in the Jolt world. Cleaned up
    // through SceneManager lifecycle events instead of polling in onUpdate().
    Scene*         _bodyOwnerScene         = nullptr;
    DelegateHandle _onAppStateChangedHandle = INVALID_HANDLE;
    DelegateHandle _onSceneActivatedHandle = INVALID_HANDLE;
    DelegateHandle _onSceneDestroyHandle   = INVALID_HANDLE;
    bool           _bSimulationActive      = false;

    float                  _accumulator    = 0.0f;
    static constexpr float kFixedDeltaTime = 1.0f / 60.0f;

  public:
    PhysicsSystem();
    ~PhysicsSystem() override;

    void setSceneManager(SceneManager* manager) { _sceneManager = manager; }
    /// Simulation follows app mode transitions (Runtime / Simulation on,
    /// Stopped off). Without a source the system defaults to always active.
    void setAppStateChangedSource(MulticastDelegate<void(AppState)>* source) { _appStateChangedSource = source; }

    void init() override;
    void onUpdate(float dt) override;
    void shutdown() override;

  private:
    void onAppStateChanged(AppState state);
    void onSceneActivated(Scene* scene);
    void onSceneDestroyed(Scene* scene);
    void reconcileBodies(entt::registry& registry);
    void writebackTransforms(entt::registry& registry);
    void clearAllBodies();
};

} // namespace ya
