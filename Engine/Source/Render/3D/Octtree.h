#pragma once
#include "Core/Base.h"

#include "glm/glm.hpp"

namespace ya
{

struct BoundingBox
{
    glm::vec3 min;
    glm::vec3 max;
};

struct OctreeNode
{
    int                                        _depth;
    std::array<std::unique_ptr<OctreeNode>, 8> _children;

    int getChildIdx(const glm::vec3 pos)
    {
    }
};

} // namespace ya