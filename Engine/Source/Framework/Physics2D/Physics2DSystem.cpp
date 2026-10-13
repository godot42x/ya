#include "Physics2D/Physics2DSystem.h"

#include "Core/Common/AppState.h"

#include "Core/Delegate.h"
#include "Core/Log.h"
#include "Scene/Core/Scene.h"
#include "Scene/Runtime/SceneManager.h"
#include "Scene3D/TransformComponent.h"

// Box2D headers stay inside this translation unit (behind the World pimpl).
#include <box2d/box2d.h>

#include <algorithm>

namespace ya
{

namespace
{

// Max physics time buffered across frames; above this we drop simulation time
// instead of entering a catch-up spiral after a hitch.
constexpr float kMaxBufferedTime = 0.25f;

b2BodyType toBox2D(EBody2DType type)
{
    switch (type) {
    case EBody2DType::Static:
        return b2_staticBody;
    case EBody2DType::Kinematic:
        return b2_kinematicBody;
    case EBody2DType::Dynamic:
        break;
    }
    return b2_dynamicBody;
}

} // namespace

struct Physics2DSystem::World
{
    b2WorldId                     world     = b2_nullWorldId;
    bool                          bCreated  = false;
    std::vector<Contact2D>        contacts;                 ///< this frame's begin/end events
    std::unordered_map<uint32_t, b2BodyId>      bodyIds;    ///< entity handle -> body
    std::unordered_map<uint64_t, uint32_t>      shapeOwner; ///< packed shape id -> entity handle
    /// Gravity direction; down the world -y, matching screen "up".
    b2Vec2 gravity{0.0f, -9.81f};
};

Physics2DSystem::Physics2DSystem()  = default;
Physics2DSystem::~Physics2DSystem()
{
    shutdown();
}

void Physics2DSystem::init()
{
    if (!_world) {
        _world = std::make_unique<World>();
    }
    if (!_world->bCreated) {
        b2WorldDef def   = b2DefaultWorldDef();
        def.gravity      = _world->gravity;
        _world->world    = b2CreateWorld(&def);
        _world->bCreated = true;
    }
    // Scene lifecycle events drive body cleanup, exactly like the 3D system.
    if (_appStateChangedSource) {
        _onAppStateChangedHandle = _appStateChangedSource->addLambda(
            this, [this](AppState state) { onAppStateChanged(state); });
    }
    if (_sceneManager) {
        _onSceneActivatedHandle = _sceneManager->onSceneActivated.addLambda(
            this, [this](Scene* scene) { onSceneActivated(scene); });
        _onSceneDestroyHandle = _sceneManager->onSceneDestroy.addLambda(
            this, [this](Scene* scene) { onSceneDestroyed(scene); });
    }
}

void Physics2DSystem::shutdown()
{
    clearAllBodies();
    if (_world && _world->bCreated) {
        b2DestroyWorld(_world->world);
        _world->bCreated = false;
    }
    if (_appStateChangedSource && _onAppStateChangedHandle != INVALID_HANDLE) {
        _appStateChangedSource->remove(_onAppStateChangedHandle);
        _onAppStateChangedHandle = INVALID_HANDLE;
    }
    if (_sceneManager) {
        if (_onSceneActivatedHandle != INVALID_HANDLE) {
            _sceneManager->onSceneActivated.remove(_onSceneActivatedHandle);
            _onSceneActivatedHandle = INVALID_HANDLE;
        }
        if (_onSceneDestroyHandle != INVALID_HANDLE) {
            _sceneManager->onSceneDestroy.remove(_onSceneDestroyHandle);
            _onSceneDestroyHandle = INVALID_HANDLE;
        }
    }
}

const std::vector<Contact2D>& Physics2DSystem::contacts() const
{
    static const std::vector<Contact2D> empty;
    return _world ? _world->contacts : empty;
}

void Physics2DSystem::onUpdate(float dt)
{
    const bool bSimulationActive = _appStateChangedSource ? _bSimulationActive : true;
    if (!bSimulationActive || !_world || !_world->bCreated) {
        return;
    }
    Scene* const scene = _sceneManager ? _sceneManager->getActiveScene() : nullptr;
    if (scene == nullptr) {
        return;
    }
    if (_bodyOwnerScene != scene) {
        clearAllBodies();
        _bodyOwnerScene = scene;
    }

    auto& registry = scene->getRegistry();
    reconcileBodies(registry);

    _world->contacts.clear();
    _accumulator = std::min(_accumulator + dt, kMaxBufferedTime);
    while (_accumulator >= kFixedDeltaTime) {
        b2World_Step(_world->world, kFixedDeltaTime, 4);
        collectContacts();
        _accumulator -= kFixedDeltaTime;
    }

    writebackTransforms(registry);
}

void Physics2DSystem::onAppStateChanged(AppState state)
{
    _bSimulationActive = (state == AppState::Runtime || state == AppState::Simulation);
    if (!_bSimulationActive) {
        clearAllBodies();
        _bodyOwnerScene = nullptr;
        _accumulator    = 0.0f;
    }
}

void Physics2DSystem::onSceneActivated(Scene* scene)
{
    if (_bodyOwnerScene != nullptr && scene != _bodyOwnerScene) {
        clearAllBodies();
    }
    _bodyOwnerScene = scene;
}

void Physics2DSystem::onSceneDestroyed(Scene* scene)
{
    if (_bodyOwnerScene == scene) {
        clearAllBodies();
        _bodyOwnerScene = nullptr;
    }
}

void Physics2DSystem::reconcileBodies(entt::registry& registry)
{
    if (!_world) {
        return;
    }
    // Destroy a body whose entity lost its transform or rigid body.
    for (auto it = _world->bodyIds.begin(); it != _world->bodyIds.end();) {
        const bool bHasBody =
            registry.all_of<TransformComponent, RigidBody2DComponent>(static_cast<entt::entity>(it->first));
        if (!bHasBody) {
            b2DestroyBody(it->second);
            it = _world->bodyIds.erase(it);
        } else {
            ++it;
        }
    }
    // Create a body for every entity that has one and no body yet.
    for (auto [handle, transform, body] : registry.view<TransformComponent, RigidBody2DComponent>().each()) {
        const uint32_t id = static_cast<uint32_t>(handle);
        if (_world->bodyIds.contains(id)) {
            continue;
        }
        const glm::vec3 position = transform.getPosition();
        b2BodyDef       def      = b2DefaultBodyDef();
        def.type                 = toBox2D(body.type);
        def.position             = {position.x, position.y};
        def.gravityScale         = body.gravityScale;
        def.fixedRotation        = body.fixedRotation;
        def.linearDamping        = body.linearDamping;
        const b2BodyId bodyId    = b2CreateBody(_world->world, &def);

        if (const Collider2DComponent* collider = registry.try_get<Collider2DComponent>(handle)) {
            b2ShapeDef shapeDef       = b2DefaultShapeDef();
            shapeDef.isSensor         = collider->bSensor;
            // Box2D v3 carries friction/restitution in the surface material.
            shapeDef.material.friction    = collider->friction;
            shapeDef.material.restitution = collider->restitution;
            shapeDef.filter.categoryBits = collider->categoryBits;
            shapeDef.filter.maskBits     = collider->maskBits;
            // Contact events are what a sensor (pressure plate, hazard) needs.
            shapeDef.enableContactEvents = true;
            const b2Vec2 offset{collider->offset.x, collider->offset.y};
            b2ShapeId    shapeId{};
            if (collider->shape == ECollider2DShape::Circle) {
                b2Circle circle{offset, std::max(collider->size.x, 0.001f)};
                shapeId = b2CreateCircleShape(bodyId, &shapeDef, &circle);
            } else {
                b2Polygon box = b2MakeOffsetBox(std::max(collider->size.x, 0.001f),
                                                std::max(collider->size.y, 0.001f), offset, b2MakeRot(0.0f));
                shapeId       = b2CreatePolygonShape(bodyId, &shapeDef, &box);
            }
            _world->shapeOwner[b2StoreShapeId(shapeId)] = id;
        }
        _world->bodyIds[id] = bodyId;
    }
}

void Physics2DSystem::writebackTransforms(entt::registry& registry)
{
    if (!_world) {
        return;
    }
    for (auto& [id, bodyId] : _world->bodyIds) {
        const entt::entity handle = static_cast<entt::entity>(id);
        if (!registry.valid(handle)) {
            continue;
        }
        TransformComponent* transform = registry.try_get<TransformComponent>(handle);
        if (transform == nullptr) {
            continue;
        }
        const b2Vec2 position = b2Body_GetPosition(bodyId);
        glm::vec3    current  = transform->getPosition();
        // z is the authoring depth (sprite ordering / 3D occlusion); the body
        // owns only the plane.
        transform->setPosition({position.x, position.y, current.z});
    }
}

void Physics2DSystem::collectContacts()
{
    if (!_world) {
        return;
    }
    const b2ContactEvents events = b2World_GetContactEvents(_world->world);
    const auto             ownerOf = [this](b2ShapeId shape) -> uint32_t {
        const auto it = _world->shapeOwner.find(b2StoreShapeId(shape));
        return it != _world->shapeOwner.end() ? it->second : 0u;
    };
    for (int i = 0; i < events.beginCount; ++i) {
        const b2ContactBeginTouchEvent& e = events.beginEvents[i];
        Contact2D c{.a = ownerOf(e.shapeIdA), .b = ownerOf(e.shapeIdB), .bBegan = true};
        if (c.a != 0 && c.b != 0) {
            _world->contacts.push_back(c);
        }
    }
    for (int i = 0; i < events.endCount; ++i) {
        const b2ContactEndTouchEvent& e = events.endEvents[i];
        Contact2D c{.a = ownerOf(e.shapeIdA), .b = ownerOf(e.shapeIdB), .bBegan = false};
        if (c.a != 0 && c.b != 0) {
            _world->contacts.push_back(c);
        }
    }
}

void Physics2DSystem::clearAllBodies()
{
    if (!_world) {
        return;
    }
    for (auto& [id, bodyId] : _world->bodyIds) {
        b2DestroyBody(bodyId);
    }
    _world->bodyIds.clear();
    _world->shapeOwner.clear();
    _world->contacts.clear();
}

void Physics2DSystem::applyImpulse(uint32_t entityId, glm::vec2 impulse)
{
    if (!_world) {
        return;
    }
    const auto it = _world->bodyIds.find(entityId);
    if (it == _world->bodyIds.end() || b2Body_GetType(it->second) != b2_dynamicBody) {
        return;
    }
    b2Body_ApplyLinearImpulseToCenter(it->second, {impulse.x, impulse.y}, true);
}

void Physics2DSystem::setBodyPosition(uint32_t entityId, glm::vec2 position)
{
    if (!_world) {
        return;
    }
    const auto it = _world->bodyIds.find(entityId);
    if (it == _world->bodyIds.end()) {
        return;
    }
    b2Body_SetTransform(it->second, {position.x, position.y}, b2Body_GetRotation(it->second));
}

glm::vec2 Physics2DSystem::bodyVelocity(uint32_t entityId) const
{
    if (!_world) {
        return {0.0f, 0.0f};
    }
    const auto it = _world->bodyIds.find(entityId);
    if (it == _world->bodyIds.end()) {
        return {0.0f, 0.0f};
    }
    const b2Vec2 v = b2Body_GetLinearVelocity(it->second);
    return {v.x, v.y};
}

void Physics2DSystem::setBodyVelocity(uint32_t entityId, glm::vec2 velocity)
{
    if (!_world) {
        return;
    }
    const auto it = _world->bodyIds.find(entityId);
    if (it == _world->bodyIds.end()) {
        return;
    }
    b2Body_SetLinearVelocity(it->second, {velocity.x, velocity.y});
}

} // namespace ya
