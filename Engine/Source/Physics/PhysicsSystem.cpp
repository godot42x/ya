#include "PhysicsSystem.h"

#include "Core/Log.h"
#include "ECS/Component/TransformComponent.h"
#include "Physics/PhysicsBodyComponent.h"
#include "Scene/Scene.h"
#include "Scene/SceneManager.h"

// Jolt headers stay inside this translation unit (behind the World pimpl).
#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include "entt/entt.hpp"

#include <algorithm>
#include <thread>
#include <unordered_map>

// Explicitly import the Jolt length / scalar literal operators instead of
// pulling the whole JPH::literals namespace into this translation unit.
using JPH::literals::operator""_r;

JPH_SUPPRESS_WARNINGS

namespace
{

// World limits. 1024 bodies / pairs is plenty for the minimal integration.
constexpr JPH::uint kMaxBodies             = 1024;
constexpr JPH::uint kNumBodyMutexes        = 0;
constexpr JPH::uint kMaxBodyPairs          = 1024;
constexpr JPH::uint kMaxContactConstraints = 1024;

// Max physics time buffered across frames; above this we drop simulation time
// instead of entering a catch-up spiral after a hitch.
constexpr float kMaxBufferedTime = 0.25f;

} // namespace

// Layer that objects can be in, determines which other objects it can collide with
namespace Layers
{
static constexpr JPH::ObjectLayer NON_MOVING = 0;
static constexpr JPH::ObjectLayer MOVING     = 1;
static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
} // namespace Layers

// Each broadphase layer results in a separate bounding volume tree in the broad phase.
namespace JPH::BroadPhaseLayers
{
static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
static constexpr JPH::BroadPhaseLayer MOVING(1);
static constexpr JPH::uint            NUM_LAYERS(2);
} // namespace JPH::BroadPhaseLayers

/// Class that determines if two object layers can collide
class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter
{
  public:
    virtual bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override
    {
        switch (inObject1) {
        case Layers::NON_MOVING:
            return inObject2 == Layers::MOVING; // Non moving only collides with moving
        case Layers::MOVING:
            return true; // Moving collides with everything
        default:
            JPH_ASSERT(false);
            return false;
        }
    }
};

// BroadPhaseLayerInterface implementation: defines the object -> broadphase layer mapping.
class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
{
  public:
    BPLayerInterfaceImpl()
    {
        mObjectToBroadPhase[Layers::NON_MOVING] = JPH::BroadPhaseLayers::NON_MOVING;
        mObjectToBroadPhase[Layers::MOVING]     = JPH::BroadPhaseLayers::MOVING;
    }

    virtual JPH::uint GetNumBroadPhaseLayers() const override
    {
        return JPH::BroadPhaseLayers::NUM_LAYERS;
    }

    virtual JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override
    {
        JPH_ASSERT(inLayer < Layers::NUM_LAYERS);
        return mObjectToBroadPhase[inLayer];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    virtual const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override
    {
        switch ((JPH::BroadPhaseLayer::Type)inLayer) {
        case (JPH::BroadPhaseLayer::Type)JPH::BroadPhaseLayers::NON_MOVING: return "NON_MOVING";
        case (JPH::BroadPhaseLayer::Type)JPH::BroadPhaseLayers::MOVING:     return "MOVING";
        default:                                                            JPH_ASSERT(false); return "INVALID";
        }
    }
#endif // JPH_EXTERNAL_PROFILE || JPH_PROFILE_ENABLED

  private:
    JPH::BroadPhaseLayer mObjectToBroadPhase[Layers::NUM_LAYERS];
};

/// Class that determines if an object layer can collide with a broadphase layer
class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter
{
  public:
    virtual bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override
    {
        switch (inLayer1) {
        case Layers::NON_MOVING:
            return inLayer2 == JPH::BroadPhaseLayers::MOVING;
        case Layers::MOVING:
            return true;
        default:
            JPH_ASSERT(false);
            return false;
        }
    }
};

// An example contact listener
class MyContactListener : public JPH::ContactListener
{
  public:
    virtual JPH::ValidateResult OnContactValidate(const JPH::Body& inBody1, const JPH::Body& inBody2, JPH::RVec3Arg inBaseOffset, const JPH::CollideShapeResult& inCollisionResult) override
    {
        YA_CORE_TRACE("Contact validate callback");
        return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
    }

    virtual void OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings) override
    {
        YA_CORE_TRACE("A contact was added");
    }

    virtual void OnContactPersisted(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings) override
    {
        YA_CORE_TRACE("A contact was persisted");
    }

    virtual void OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) override
    {
        YA_CORE_TRACE("A contact was removed");
    }
};

// An example activation listener
class MyBodyActivationListener : public JPH::BodyActivationListener
{
  public:
    virtual void OnBodyActivated(const JPH::BodyID& inBodyID, JPH::uint64 inBodyUserData) override
    {
        YA_CORE_TRACE("A body got activated");
    }

    virtual void OnBodyDeactivated(const JPH::BodyID& inBodyID, JPH::uint64 inBodyUserData) override
    {
        YA_CORE_TRACE("A body went to sleep");
    }
};

namespace ya
{

// Out-of-line so TUs that only see the pimpl declaration never instantiate
// unique_ptr<World> member cleanup (World is incomplete outside this TU).
PhysicsSystem::PhysicsSystem() = default;

// Callback for traces, connect this to your own trace function if you have one
static void TraceImpl(const char* inFMT, ...)
{
    va_list list;
    va_start(list, inFMT);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), inFMT, list);
    va_end(list);

    YA_CORE_TRACE("{}", buffer);
}

#ifdef JPH_ENABLE_ASSERTS
// Callback for asserts, connect this to your own assert handler if you have one
static bool AssertFailedImpl(const char* inExpression, const char* inMessage, const char* inFile, JPH::uint inLine)
{
    YA_CORE_ERROR("{}:{}: ({}) {}", inFile, inLine, inExpression, (inMessage != nullptr ? inMessage : ""));
    return true;
}
#endif // JPH_ENABLE_ASSERTS

// Runtime Jolt world. The layer interfaces, temp allocator and job system must
// outlive the physics system, so they live here as members declared before it.
struct PhysicsSystem::World
{
    BPLayerInterfaceImpl              broadPhaseLayerInterface;
    ObjectVsBroadPhaseLayerFilterImpl objectVsBroadPhaseLayerFilter;
    ObjectLayerPairFilterImpl         objectVsObjectLayerFilter;
    JPH::TempAllocatorImpl            tempAllocator{10 * 1024 * 1024};
    JPH::JobSystemThreadPool          jobSystem{JPH::cMaxPhysicsJobs,
                                                JPH::cMaxPhysicsBarriers,
                                                std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1)};
    JPH::PhysicsSystem                physicsSystem;
    std::unordered_map<entt::entity, JPH::BodyID> bodyIds;
};

PhysicsSystem::~PhysicsSystem() = default;

void PhysicsSystem::init()
{
    // Jolt process-global bootstrap, guarded so PIE / scene re-entries never
    // double-register the factory or the type handlers.
    if (JPH::Factory::sInstance == nullptr) {
        JPH::RegisterDefaultAllocator();
        JPH::Trace = TraceImpl;
        JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = AssertFailedImpl;)
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    }

    // World construction must happen after RegisterDefaultAllocator() because
    // the temp allocator allocates through the (previously null) Jolt hooks.
    _world = std::make_unique<World>();
    _world->physicsSystem.Init(kMaxBodies,
                               kNumBodyMutexes,
                               kMaxBodyPairs,
                               kMaxContactConstraints,
                               _world->broadPhaseLayerInterface,
                               _world->objectVsBroadPhaseLayerFilter,
                               _world->objectVsObjectLayerFilter);
    _world->physicsSystem.OptimizeBroadPhase();

    // Scene lifecycle events drive body cleanup: leaving a play session
    // activates/destroys the play scene, which drops every body immediately
    // instead of detecting the transition by polling in onUpdate().
    if (_sceneManager) {
        _onSceneActivatedHandle = _sceneManager->onSceneActivated.addLambda(this, [this](Scene* scene)
                                                                            { onSceneActivated(scene); });
        _onSceneDestroyHandle   = _sceneManager->onSceneDestroy.addLambda(this, [this](Scene* scene)
                                                                          { onSceneDestroyed(scene); });
    }
}

void PhysicsSystem::onUpdate(float dt)
{
    const bool bSimulationActive = _simulationActiveProvider ? _simulationActiveProvider() : true;
    if (!bSimulationActive || !_world) {
        return;
    }

    Scene* const scene = _activeSceneProvider ? _activeSceneProvider() : nullptr;
    if (scene == nullptr) {
        return;
    }

    // Defensive consistency for hosts without a SceneManager (or a missed
    // event): bodies always belong to the scene we are simulating.
    if (_bodyOwnerScene != scene) {
        clearAllBodies();
        _bodyOwnerScene = scene;
    }

    auto& registry = scene->getRegistry();
    reconcileBodies(registry);

    // Fixed-timestep simulation. Buffered time is capped so a hitch cannot
    // turn into an unbounded catch-up loop.
    _accumulator = std::min(_accumulator + dt, kMaxBufferedTime);
    while (_accumulator >= kFixedDeltaTime) {
        _world->physicsSystem.Update(kFixedDeltaTime, 1, &_world->tempAllocator, &_world->jobSystem);
        _accumulator -= kFixedDeltaTime;
    }

    writebackTransforms(registry);
}

void PhysicsSystem::onSceneActivated(Scene* scene)
{
    if (_bodyOwnerScene != nullptr && scene != _bodyOwnerScene) {
        clearAllBodies();
    }
    _bodyOwnerScene = scene;
}

void PhysicsSystem::onSceneDestroyed(Scene* scene)
{
    if (_bodyOwnerScene == scene) {
        clearAllBodies();
        _bodyOwnerScene = nullptr;
    }
}

void PhysicsSystem::reconcileBodies(entt::registry& registry)
{
    if (!_world) {
        return;
    }
    JPH::BodyInterface& bodyInterface = _world->physicsSystem.GetBodyInterface();

    // Destroy bodies whose entity no longer has a transform or physics body.
    for (auto it = _world->bodyIds.begin(); it != _world->bodyIds.end();) {
        if (!registry.all_of<TransformComponent, PhysicsBodyComponent>(it->first)) {
            bodyInterface.RemoveBody(it->second);
            bodyInterface.DestroyBody(it->second);
            it = _world->bodyIds.erase(it);
        } else {
            ++it;
        }
    }

    // Create a body for every entity that does not have one yet.
    for (auto&& [entity, transform, bodyComponent] :
         registry.view<TransformComponent, PhysicsBodyComponent>().each()) {
        if (_world->bodyIds.contains(entity)) {
            continue;
        }

        JPH::ShapeRefC shape;
        if (bodyComponent._shape == PhysicsBodyShape::Sphere) {
            shape = new JPH::SphereShape(PhysicsBodyComponent::kDefaultSphereRadius);
        } else {
            const float halfExtent = PhysicsBodyComponent::kDefaultBoxHalfExtent;
            shape                  = new JPH::BoxShape(JPH::Vec3(halfExtent, halfExtent, halfExtent));
        }

        const glm::vec3& position  = transform.getPosition();
        const glm::quat  rotation  = glm::quat(glm::radians(transform.getRotation()));
        const bool       isDynamic = bodyComponent._isDynamic;

        JPH::BodyCreationSettings bodySettings(
            shape,
            JPH::RVec3(position.x, position.y, position.z),
            JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w),
            isDynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static,
            isDynamic ? Layers::MOVING : Layers::NON_MOVING);

        JPH::Body* const body = bodyInterface.CreateBody(bodySettings);
        if (body == nullptr) {
            continue; // Out of body slots; keep going for the other entities.
        }
        bodyInterface.AddBody(body->GetID(),
                              isDynamic ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
        _world->bodyIds[entity] = body->GetID();
    }
}

void PhysicsSystem::writebackTransforms(entt::registry& registry)
{
    if (!_world) {
        return;
    }
    JPH::BodyInterface& bodyInterface = _world->physicsSystem.GetBodyInterface();

    for (auto&& [entity, transform, bodyComponent] :
         registry.view<TransformComponent, PhysicsBodyComponent>().each()) {
        if (!bodyComponent._isDynamic) {
            continue; // Static bodies never move.
        }

        const auto it = _world->bodyIds.find(entity);
        if (it == _world->bodyIds.end()) {
            continue;
        }

        const JPH::RVec3 bodyPosition = bodyInterface.GetCenterOfMassPosition(it->second);
        const JPH::Quat  bodyRotation = bodyInterface.GetRotation(it->second);

        transform.setPosition(glm::vec3(static_cast<float>(bodyPosition.GetX()),
                                        static_cast<float>(bodyPosition.GetY()),
                                        static_cast<float>(bodyPosition.GetZ())));

        // JPH::Quat stores (x, y, z, w); glm::quat stores (w, x, y, z).
        const glm::quat glmQuat(bodyRotation.GetW(), bodyRotation.GetX(), bodyRotation.GetY(), bodyRotation.GetZ());
        transform.setRotation(glm::degrees(glm::eulerAngles(glmQuat)));
    }
}

void PhysicsSystem::clearAllBodies()
{
    if (!_world) {
        return;
    }
    JPH::BodyInterface& bodyInterface = _world->physicsSystem.GetBodyInterface();
    for (const auto& [entity, bodyId] : _world->bodyIds) {
        bodyInterface.RemoveBody(bodyId);
        bodyInterface.DestroyBody(bodyId);
    }
    _world->bodyIds.clear();
}

void PhysicsSystem::shutdown()
{
    if (_sceneManager) {
        if (_onSceneActivatedHandle != INVALID_HANDLE) {
            _sceneManager->onSceneActivated.remove(_onSceneActivatedHandle);
            _onSceneActivatedHandle = INVALID_HANDLE;
        }
        if (_onSceneDestroyHandle != INVALID_HANDLE) {
            _sceneManager->onSceneDestroy.remove(_onSceneDestroyHandle);
            _onSceneDestroyHandle = INVALID_HANDLE;
        }
        // Safety net: drop any leftover owner-scoped callbacks.
        _sceneManager->onSceneActivated.removeAll(this);
        _sceneManager->onSceneDestroy.removeAll(this);
    }

    clearAllBodies();
    _bodyOwnerScene = nullptr;
    _world.reset();

    if (JPH::Factory::sInstance != nullptr) {
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }
}

} // namespace ya

JPH_SUPPRESS_WARNING_POP
