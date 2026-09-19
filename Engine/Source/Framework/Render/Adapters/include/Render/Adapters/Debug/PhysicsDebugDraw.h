#pragma once

#include "Core/Api.h"

#include <functional>

#include <glm/glm.hpp>

namespace ya
{

struct Scene;

/// Injected line-draw sink for physics collision debug primitives. The caller
/// (editor viewport composition) bridges this to its overlay renderer; the
/// debug draw never reaches Render2D directly.
///
/// Lives in the adapter layer, not in ya-render-3d: reading physics bodies and
/// deciding which wireframe shape represents them is a bridge between an ECS
/// component and render geometry, which is what this module is for. Keeping it
/// in the renderer made the *runtime renderer* depend on the physics module for
/// one debug overlay and know the body shape enum and default radii.
struct PhysicsDebugLineCollector
{
    std::function<void(const glm::vec3& center, float radius, const glm::vec4& color)> sphere;
    std::function<void(const glm::mat4& model, const glm::vec3& halfExtent, const glm::vec4& color)> box;
};

/**
 * @brief Draw wireframe overlays for every physics body in the given scene.
 * Shapes and sizes stay in sync with the body creation rules in PhysicsSystem
 * via PhysicsBodyComponent constants. The line collector is injected by the
 * caller.
 */
YA_RENDER_ECS_ADAPTERS_API void drawPhysicsCollisionDebug(Scene& scene, const PhysicsDebugLineCollector& collector);

} // namespace ya
