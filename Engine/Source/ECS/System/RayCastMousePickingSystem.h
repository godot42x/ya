#pragma once

#include "Core/Math/GLM.h"
#include "Core/Math/Ray.h"
#include <optional>

namespace ya
{

struct Entity;
struct Scene;
struct FreeCamera;

/**
 * @brief Result of a raycast operation
 */
struct RaycastHit
{
    Entity   *entity   = nullptr;
    float     distance = 0.0f;
    glm::vec3 point    = glm::vec3(0.0f);
};

/**
 * @brief System for mouse picking using raycast
 *
 * Handles conversion from screen coordinates to world-space rays
 * and collision detection with scene entities.
 */
class RayCastMousePickingSystem
{
  public:
    RayCastMousePickingSystem()  = default;
    ~RayCastMousePickingSystem() = default;

    /**
     * @brief Perform raycast against scene entities
     * @param scene Scene to raycast in
     * @param ray Ray to cast
     * @return Hit information if any entity was hit
     */
    static std::optional<RaycastHit> raycast(Scene *scene, const Ray &ray);

    /**
     * @brief Pick entity at screen coordinates
     * @param scene Scene to pick from
     * @param screenX Screen X coordinate
     * @param screenY Screen Y coordinate
     * @param viewportWidth Viewport width
     * @param viewportHeight Viewport height
     * @param camera Camera
     * @return Picked entity or nullptr
     */
    static Entity *pickEntity(Scene    *scene,
                              float     screenX,
                              float     screenY,
                              float     viewportWidth,
                              float     viewportHeight,
                              glm::mat4 viewMatrix,
                              glm::mat4 projectionMatrix);
};

} // namespace ya
