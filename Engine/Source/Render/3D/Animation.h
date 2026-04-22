#pragma once

#include "Core/Base.h"

namespace ya
{



struct KeyPosition
{
    glm::vec3 pos;
    float     timestamp;
};

struct KeyRotation
{
    glm::quat orientation;
    float     timestamp;
};

struct KeyScale
{
    glm::vec3 scale;
    float     timestamp;
};


struct Bone
{
    std::vector<KeyPosition> _positions;
    std::vector<KeyRotation> _rotations;
    std::vector<KeyScale>    _scales;

    glm::mat4 localTransf;
    FName     name;
};
} // namespace ya