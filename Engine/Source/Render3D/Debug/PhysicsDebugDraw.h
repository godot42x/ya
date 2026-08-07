#pragma once

#include "Core/Api.h"

namespace ya
{

struct Scene;

/**
 * @brief Draw wireframe overlays for every physics body in the given scene.
 *
 * Must be called inside a Render2D::begin() / Render2D::end() window (the
 * editor viewport composition pass). Shapes and sizes stay in sync with the
 * body creation rules in PhysicsSystem via PhysicsBodyComponent constants.
 */
YA_RENDER_3D_API void drawPhysicsCollisionDebug(Scene& scene);

} // namespace ya
