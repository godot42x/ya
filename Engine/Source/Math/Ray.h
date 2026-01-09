#pragma once

#include "Math/GLM.h"
#include "Math/AABB.h"

namespace ya
{

/**
 * @brief Simple ray for picking and intersection tests
 */
struct Ray
{
    glm::vec3 origin;
    glm::vec3 direction; // Should be normalized

    Ray() = default;
    Ray(const glm::vec3 &orig, const glm::vec3 &dir)
        : origin(orig), direction(glm::normalize(dir)) {}

    /**
     * @brief Get point along ray at distance t
     */
    glm::vec3 at(float t) const { return origin + direction * t; }

    /**
     * @brief Test intersection with AABB
     * @param aabb Axis-aligned bounding box
     * @param outDistance Distance to intersection point (if hit)
     * @return true if ray intersects AABB
     */
    bool intersects(const AABB &aabb, float *outDistance = nullptr) const
    {
        glm::vec3 invDir = 1.0f / direction;
        glm::vec3 t0 = (aabb.min - origin) * invDir;
        glm::vec3 t1 = (aabb.max - origin) * invDir;

        glm::vec3 tmin = glm::min(t0, t1);
        glm::vec3 tmax = glm::max(t0, t1);

        float tNear = glm::max(glm::max(tmin.x, tmin.y), tmin.z);
        float tFar = glm::min(glm::min(tmax.x, tmax.y), tmax.z);

        if (tNear > tFar || tFar < 0.0f) {
            return false;
        }

        if (outDistance) {
            *outDistance = tNear > 0.0f ? tNear : tFar;
        }

        return true;
    }

    /**
     * @brief Create ray from screen coordinates
     * @param screenX Screen X coordinate
     * @param screenY Screen Y coordinate
     * @param viewportWidth Viewport width
     * @param viewportHeight Viewport height
     * @param view View matrix
     * @param projection Projection matrix
     * @return Ray in world space
     */
    static Ray fromScreen(
        float screenX,
        float screenY,
        float viewportWidth,
        float viewportHeight,
        const glm::mat4 &view,
        const glm::mat4 &projection)
    {
        // Convert screen coordinates to NDC [-1, 1]
        float x = (2.0f * screenX) / viewportWidth - 1.0f;
        float y = 1.0f - (2.0f * screenY) / viewportHeight; // Flip Y

        // NDC to clip space
        glm::vec4 rayClip(x, y, -1.0f, 1.0f);

        // Clip to eye space
        glm::vec4 rayEye = glm::inverse(projection) * rayClip;
        rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0f, 0.0f);

        // Eye to world space
        glm::vec3 rayWorld = glm::vec3(glm::inverse(view) * rayEye);
        rayWorld = glm::normalize(rayWorld);

        // Get camera position from view matrix
        glm::vec3 cameraPos = glm::vec3(glm::inverse(view)[3]);

        return Ray(cameraPos, rayWorld);
    }
};

} // namespace ya
