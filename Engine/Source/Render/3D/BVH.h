/**
BVH(Bounding Volume Hierarchy)
*/
#pragma once

#include <glm/glm.hpp>

namespace ya
{

struct BoundingBox
{
    glm::vec3 min;
    glm::vec3 max;
};


}; // namespace ya