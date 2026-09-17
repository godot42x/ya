#pragma once

#include "Core/Base.h"

#include <glm/gtc/quaternion.hpp>

#include <string>
#include <vector>

namespace ya
{

struct ImportedSkeletonVectorKeyframe
{
    double    time = 0.0;
    glm::vec3 value{0.0f};
};

struct ImportedSkeletonQuatKeyframe
{
    double    time = 0.0;
    glm::quat value{1.0f, 0.0f, 0.0f, 0.0f};
};

struct ImportedSkeletonAnimationChannel
{
    FName targetName;

    std::vector<ImportedSkeletonVectorKeyframe> positionKeys;
    std::vector<ImportedSkeletonQuatKeyframe>   rotationKeys;
    std::vector<ImportedSkeletonVectorKeyframe> scaleKeys;
};

struct ImportedSkeletonAnimationClip
{
    std::string                                   name;
    double                                        duration       = 0.0;
    double                                        ticksPerSecond = 0.0;
    std::vector<ImportedSkeletonAnimationChannel> channels;
};

} // namespace ya
