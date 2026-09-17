#pragma once

#include "ECS/Component.h"

namespace ya
{

enum class PhysicsBodyShape
{
    Box,
    Sphere,
};

/**
 * @brief PhysicsBodyComponent - Minimum authoring data describing a rigid body.
 *
 * The runtime Jolt body itself lives in PhysicsSystem; this component only
 * describes what kind of body an entity should have. Entities need a
 * TransformComponent next to it so the body can be placed in the world.
 */
struct PhysicsBodyComponent : public IComponent
{
    YA_REFLECT_BEGIN(PhysicsBodyComponent)
    YA_REFLECT_FIELD(_shape)
    YA_REFLECT_FIELD(_isDynamic)
    YA_REFLECT_END()

    // Single source of truth for the v1 body sizes, shared by the body
    // creation (PhysicsSystem) and the debug overlay (PhysicsDebugDraw).
    static constexpr float kDefaultBoxHalfExtent = 0.5f;
    static constexpr float kDefaultSphereRadius  = 0.5f;

    PhysicsBodyShape _shape     = PhysicsBodyShape::Box;
    bool             _isDynamic = true;
};

} // namespace ya


YA_REFLECT_ENUM_BEGIN(ya::PhysicsBodyShape)
YA_REFLECT_ENUM_VALUE(Box)
YA_REFLECT_ENUM_VALUE(Sphere)
YA_REFLECT_ENUM_END()