#pragma once

#include "Core/Api.h"
#include "Core/Reflection/Reflection.h"
#include "ECS/Component.h"

#include <glm/glm.hpp>

#include <cstdint>

namespace ya
{

/// How a 2D body participates in the simulation.
enum class EBody2DType : uint8_t
{
    Static    = 0,  ///< Never moves; a floor, a wall.
    Kinematic = 1,  ///< Moved by its transform, pushes dynamic bodies.
    Dynamic   = 2,  ///< Moved by the simulation (gravity, impulses, pushes).
};

/// Shape of a Collider2DComponent.
enum class ECollider2DShape : uint8_t
{
    Box    = 0,
    Circle = 1,
};

/// A 2D rigid body on a Node3D, next to a TransformComponent and a
/// Collider2DComponent. The runtime Box2D body lives in Physics2DSystem; this
/// component only says what kind of body an entity has and how it behaves.
///
/// The body is 2D: the system reads and writes only the transform's x/y and
/// leaves z alone, so an entity on the same tree as 3D objects is unaffected.
struct YA_PHYSICS_2D_API RigidBody2DComponent : public IComponent
{
    YA_REFLECT_BEGIN(RigidBody2DComponent, IComponent)
    YA_REFLECT_FIELD(type)
    YA_REFLECT_FIELD(gravityScale)
    YA_REFLECT_FIELD(fixedRotation)
    YA_REFLECT_FIELD(linearDamping)
    YA_REFLECT_END()

    EBody2DType type          = EBody2DType::Dynamic;
    /// Multiplies world gravity for this body. 0 disables gravity.
    float       gravityScale  = 1.0f;
    /// Keep the body upright (no rotation from collisions); typical for a
    /// character or a crate that must not tumble.
    bool        fixedRotation = false;
    float       linearDamping = 0.0f;
};

/// A 2D collider: a box (half-extents) or a circle (radius), offset from the
/// transform. `bSensor` makes it a trigger: it reports contacts but applies no
/// impulse, which is what a pressure plate or a hazard zone needs.
struct YA_PHYSICS_2D_API Collider2DComponent : public IComponent
{
    YA_REFLECT_BEGIN(Collider2DComponent, IComponent)
    YA_REFLECT_FIELD(shape)
    YA_REFLECT_FIELD(size)          ///< Half-extents for Box, radius for Circle
    YA_REFLECT_FIELD(offset)
    YA_REFLECT_FIELD(bSensor)
    YA_REFLECT_FIELD(friction)
    YA_REFLECT_FIELD(restitution)
    YA_REFLECT_FIELD(categoryBits)
    YA_REFLECT_FIELD(maskBits)
    YA_REFLECT_END()

    ECollider2DShape shape = ECollider2DShape::Box;
    /// Half-extents for a Box, radius for a Circle (x is used).
    glm::vec2        size{0.5f, 0.5f};
    glm::vec2        offset{0.0f};
    /// Sensor (trigger): reports contacts, never applies an impulse.
    bool  bSensor     = false;
    float friction    = 0.3f;
    float restitution = 0.0f;
    /// Collision filtering: two colliders interact when
    /// `(a.category & b.mask) != 0 && (b.category & a.mask) != 0`.
    uint16_t categoryBits = 0x0001;
    uint16_t maskBits     = 0xFFFF;
};

/// One contact between two colliders, reported by Physics2DSystem each step.
/// `a` and `b` are entity ids (the entt handle), so a script can resolve them.
struct Contact2D
{
    uint32_t a      = 0;
    uint32_t b      = 0;
    bool     bBegan = true;  ///< true = began this step, false = ended
    /// Contact point (world), average of the manifold points, for effects.
    glm::vec2 point{0.0f};
};

} // namespace ya

YA_REFLECT_ENUM_BEGIN(ya::EBody2DType)
YA_REFLECT_ENUM_VALUE(Static)
YA_REFLECT_ENUM_VALUE(Kinematic)
YA_REFLECT_ENUM_VALUE(Dynamic)
YA_REFLECT_ENUM_END()

YA_REFLECT_ENUM_BEGIN(ya::ECollider2DShape)
YA_REFLECT_ENUM_VALUE(Box)
YA_REFLECT_ENUM_VALUE(Circle)
YA_REFLECT_ENUM_END()
