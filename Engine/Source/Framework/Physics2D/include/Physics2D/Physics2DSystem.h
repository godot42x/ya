#pragma once

#include "Core/Api.h"
#include "Core/Delegate.h"
#include "Core/System/System.h"
#include "Physics2D/Physics2DComponents.h"

#include "entt/entt.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace ya
{

struct Scene;
struct SceneManager;
enum class AppState;

/**
 * @brief 2D rigid-body simulation on Box2D.
 *
 * Every entity carrying TransformComponent + RigidBody2DComponent (and usually
 * Collider2DComponent) gets a Box2D body. The world steps at a fixed 60 Hz and
 * each body's position is written back into the entity's transform's x/y (z is
 * left alone). Contact events between sensors and other colliders are collected
 * into `contacts()` for the frame, so a script can react to a pressure plate or
 * a hazard zone without polling geometry.
 *
 * All Box2D state lives behind the pimpl, so this header (and every TU that
 * includes it) stays free of Box2D headers.
 */
struct YA_PHYSICS_2D_API Physics2DSystem : public ISystem
{
  public:
    struct World;

    Physics2DSystem();
    ~Physics2DSystem() override;

    void setSceneManager(SceneManager* manager) { _sceneManager = manager; }
    /// Simulation follows app mode transitions (Runtime / Simulation on,
    /// Stopped off). Without a source the system defaults to always active.
    void setAppStateChangedSource(MulticastDelegate<void(AppState)>* source) { _appStateChangedSource = source; }

    void init() override;
    void onUpdate(float dt) override;
    void shutdown() override;

    /// Contacts that began or ended during the last step. Valid until the next
    /// `onUpdate`; a script reads this in its own update of the same frame.
    [[nodiscard]] const std::vector<Contact2D>& contacts() const;

    /// Apply a linear impulse (x, y) to a dynamic body at its centre. No-op for
    /// a static/kinematic body or an entity without a body.
    void applyImpulse(uint32_t entityId, glm::vec2 impulse);
    /// Set a kinematic/dynamic body's transform immediately (teleport) and wake
    /// it. Used by a moving platform or a scripted shove.
    void setBodyPosition(uint32_t entityId, glm::vec2 position);
    /// Linear velocity of a body, or (0,0) when there is none.
    [[nodiscard]] glm::vec2 bodyVelocity(uint32_t entityId) const;
    /// Set a body's linear velocity (dynamic only).
    void setBodyVelocity(uint32_t entityId, glm::vec2 velocity);

  private:
    std::unique_ptr<World> _world;

    MulticastDelegate<void(AppState)>* _appStateChangedSource = nullptr;
    SceneManager*                      _sceneManager         = nullptr;
    Scene*                             _bodyOwnerScene       = nullptr;
    DelegateHandle                     _onAppStateChangedHandle = INVALID_HANDLE;
    DelegateHandle                     _onSceneActivatedHandle  = INVALID_HANDLE;
    DelegateHandle                     _onSceneDestroyHandle    = INVALID_HANDLE;
    bool                               _bSimulationActive    = false;

    float                  _accumulator    = 0.0f;
    static constexpr float kFixedDeltaTime = 1.0f / 60.0f;

    void onAppStateChanged(AppState state);
    void onSceneActivated(Scene* scene);
    void onSceneDestroyed(Scene* scene);
    void reconcileBodies(entt::registry& registry);
    void writebackTransforms(entt::registry& registry);
    void collectContacts();
    void clearAllBodies();
};

} // namespace ya
