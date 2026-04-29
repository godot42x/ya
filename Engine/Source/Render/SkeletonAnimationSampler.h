#pragma once

#include "Render/Skeleton.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace ya
{

struct SkeletonChannelSample
{
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
};

struct SkeletonPose
{
    std::vector<glm::mat4> localTransforms;
    std::vector<glm::mat4> globalTransforms;
    std::vector<glm::mat4> boneMatrices;
};

struct SkeletonAnimationSampler
{
    static double wrapTime(double time, double duration);

    static glm::vec3 sampleVectorTrack(const std::vector<SkeletonVectorKeyframe>& keys,
                                       double                                     time,
                                       const glm::vec3&                           fallback = glm::vec3(0.0f));
    static glm::quat sampleQuatTrack(const std::vector<SkeletonQuatKeyframe>& keys,
                                     double                                   time,
                                     const glm::quat&                         fallback = glm::quat(1.0f, 0.0f, 0.0f, 0.0f));

    static SkeletonChannelSample sampleChannel(const SkeletonAnimationChannel& channel,
                                               double                          time,
                                               const SkeletonNodeInfo*         bindNode = nullptr);

    static SkeletonPose samplePose(const Skeleton&              skeleton,
                                   const SkeletonAnimationClip& clip,
                                   double                       time,
                                   bool                         loop = true);
};

} // namespace ya
