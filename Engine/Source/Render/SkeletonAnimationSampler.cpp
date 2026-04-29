#include "SkeletonAnimationSampler.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace ya
{
namespace
{
constexpr float ZERO_SCALE_EPSILON = 1e-6f;

float interpolationFactor(double lhsTime, double rhsTime, double time)
{
    const double range = rhsTime - lhsTime;
    if (range <= 0.0) {
        return 1.0f;
    }
    return static_cast<float>(std::clamp((time - lhsTime) / range, 0.0, 1.0));
}

size_t findVectorKeyframeIndex(const std::vector<SkeletonVectorKeyframe>& keys, double time)
{
    const auto upper = std::upper_bound(keys.begin(), keys.end(), time, [](double value, const SkeletonVectorKeyframe& key)
                                        { return value < key.time; });
    return static_cast<size_t>(std::distance(keys.begin(), upper) - 1);
}

size_t findQuatKeyframeIndex(const std::vector<SkeletonQuatKeyframe>& keys, double time)
{
    const auto upper = std::upper_bound(keys.begin(), keys.end(), time, [](double value, const SkeletonQuatKeyframe& key)
                                        { return value < key.time; });
    return static_cast<size_t>(std::distance(keys.begin(), upper) - 1);
}

SkeletonChannelSample decomposeTransform(const glm::mat4& transform)
{
    SkeletonChannelSample sample;
    sample.position = glm::vec3(transform[3]);

    sample.scale = glm::vec3(
        glm::length(glm::vec3(transform[0])),
        glm::length(glm::vec3(transform[1])),
        glm::length(glm::vec3(transform[2])));

    glm::mat3 rotation(1.0f);
    rotation[0]     = sample.scale.x > ZERO_SCALE_EPSILON ? glm::vec3(transform[0]) / sample.scale.x : glm::vec3(1.0f, 0.0f, 0.0f);
    rotation[1]     = sample.scale.y > ZERO_SCALE_EPSILON ? glm::vec3(transform[1]) / sample.scale.y : glm::vec3(0.0f, 1.0f, 0.0f);
    rotation[2]     = sample.scale.z > ZERO_SCALE_EPSILON ? glm::vec3(transform[2]) / sample.scale.z : glm::vec3(0.0f, 0.0f, 1.0f);
    sample.rotation = glm::normalize(glm::quat_cast(rotation));

    return sample;
}

constexpr glm::mat4 identityTransform()
{
    return glm::mat4(1.0f);
}

glm::mat4 composeTransform(const SkeletonChannelSample& sample)
{
    return glm::translate(identityTransform(), sample.position) *
           glm::mat4_cast(glm::normalize(sample.rotation)) *
           glm::scale(identityTransform(), sample.scale);
}

double sampleTimeForClip(double time, double duration, bool loop)
{
    if (duration <= 0.0) {
        return 0.0;
    }
    if (loop) {
        return SkeletonAnimationSampler::wrapTime(time, duration);
    }
    return std::clamp(time, 0.0, duration);
}

} // namespace

double SkeletonAnimationSampler::wrapTime(double time, double duration)
{
    if (duration <= 0.0) {
        return 0.0;
    }

    double wrapped = std::fmod(time, duration);
    if (wrapped < 0.0) {
        wrapped += duration;
    }
    return wrapped;
}

glm::vec3 SkeletonAnimationSampler::sampleVectorTrack(const std::vector<SkeletonVectorKeyframe>& keys,
                                                      double                                     time,
                                                      const glm::vec3&                           fallback)
{
    if (keys.empty()) {
        return fallback;
    }
    if (keys.size() == 1 || time <= keys.front().time) {
        return keys.front().value;
    }
    if (time >= keys.back().time) {
        return keys.back().value;
    }

    const size_t lhsIndex = findVectorKeyframeIndex(keys, time);
    const size_t rhsIndex = lhsIndex + 1;
    const auto&  lhs      = keys[lhsIndex];
    const auto&  rhs      = keys[rhsIndex];
    return glm::mix(lhs.value, rhs.value, interpolationFactor(lhs.time, rhs.time, time));
}

glm::quat SkeletonAnimationSampler::sampleQuatTrack(const std::vector<SkeletonQuatKeyframe>& keys,
                                                    double                                   time,
                                                    const glm::quat&                         fallback)
{
    if (keys.empty()) {
        return fallback;
    }
    if (keys.size() == 1 || time <= keys.front().time) {
        return glm::normalize(keys.front().value);
    }
    if (time >= keys.back().time) {
        return glm::normalize(keys.back().value);
    }

    const size_t lhsIndex = findQuatKeyframeIndex(keys, time);
    const size_t rhsIndex = lhsIndex + 1;
    const auto&  lhs      = keys[lhsIndex];
    const auto&  rhs      = keys[rhsIndex];
    return glm::normalize(glm::slerp(lhs.value, rhs.value, interpolationFactor(lhs.time, rhs.time, time)));
}

SkeletonChannelSample SkeletonAnimationSampler::sampleChannel(const SkeletonAnimationChannel& channel,
                                                              double                          time,
                                                              const SkeletonNodeInfo*         bindNode)
{
    SkeletonChannelSample sample = bindNode ? decomposeTransform(bindNode->localTransform) : SkeletonChannelSample{};
    sample.position              = sampleVectorTrack(channel.positionKeys, time, sample.position);
    sample.rotation              = sampleQuatTrack(channel.rotationKeys, time, sample.rotation);
    sample.scale                 = sampleVectorTrack(channel.scaleKeys, time, sample.scale);
    return sample;
}

SkeletonPose SkeletonAnimationSampler::samplePose(const Skeleton&              skeleton,
                                                  const SkeletonAnimationClip& clip,
                                                  double                       time,
                                                  bool                         loop)
{
    SkeletonPose pose;
    pose.localTransforms.reserve(skeleton.nodes.size());
    pose.globalTransforms.resize(skeleton.nodes.size(), identityTransform());
    pose.boneMatrices.resize(skeleton.bones.size(), identityTransform());

    for (const SkeletonNodeInfo& node : skeleton.nodes) {
        pose.localTransforms.push_back(node.localTransform);
    }

    const double animationTime = sampleTimeForClip(time, clip.duration, loop);
    for (const SkeletonAnimationChannel& channel : clip.channels) {
        if (channel.nodeIndex >= skeleton.nodes.size()) {
            continue;
        }

        const SkeletonChannelSample sample      = sampleChannel(channel, animationTime, &skeleton.nodes[channel.nodeIndex]);
        pose.localTransforms[channel.nodeIndex] = composeTransform(sample);
    }

    for (size_t nodeIndex = 0; nodeIndex < skeleton.nodes.size(); ++nodeIndex) {
        const uint32_t parentIndex = skeleton.nodes[nodeIndex].parentIndex;
        if (parentIndex == INVALID_SKELETON_NODE_INDEX || parentIndex >= pose.globalTransforms.size()) {
            pose.globalTransforms[nodeIndex] = pose.localTransforms[nodeIndex];
        }
        else {
            pose.globalTransforms[nodeIndex] = pose.globalTransforms[parentIndex] * pose.localTransforms[nodeIndex];
        }
    }

    for (size_t boneIndex = 0; boneIndex < skeleton.bones.size(); ++boneIndex) {
        const SkeletonBoneInfo& bone = skeleton.bones[boneIndex];
        if (bone.nodeIndex >= pose.globalTransforms.size()) {
            continue;
        }
        pose.boneMatrices[boneIndex] = pose.globalTransforms[bone.nodeIndex] * bone.offsetMatrix;
    }

    return pose;
}

} // namespace ya
