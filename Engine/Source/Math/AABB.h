#pragma once

#include <limits>

#include "Math/GLM.h"

namespace ya
{

/**
 * @brief Axis-Aligned Bounding Box
 */
struct AABB
{
    glm::vec3 min = glm::vec3(std::numeric_limits<float>::max());
    glm::vec3 max = glm::vec3(std::numeric_limits<float>::lowest());

    AABB() = default;
    AABB(const glm::vec3 &min, const glm::vec3 &max) : min(min), max(max) {}

    void reset()
    {
        min = glm::vec3(std::numeric_limits<float>::max());
        max = glm::vec3(std::numeric_limits<float>::lowest());
    }

    void expand(const glm::vec3 &point)
    {
        min = glm::min(min, point);
        max = glm::max(max, point);
    }

    void merge(const AABB &other)
    {
        min = glm::min(min, other.min);
        max = glm::max(max, other.max);
    }

    glm::vec3 getCenter() const { return (min + max) * 0.5f; }
    glm::vec3 getExtent() const { return max - min; }
    float     getRadius() const { return glm::length(getExtent()) * 0.5f; }

    bool isValid() const
    {
        return min.x <= max.x && min.y <= max.y && min.z <= max.z;
    }
};

} // namespace ya
