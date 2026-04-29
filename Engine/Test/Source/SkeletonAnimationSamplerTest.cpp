#include "Render/SkeletonAnimationSampler.h"

#include <gtest/gtest.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>

namespace
{
constexpr float EPSILON = 1e-4f;

bool nearVec3(const glm::vec3& lhs, const glm::vec3& rhs, float epsilon = EPSILON)
{
    return glm::length(lhs - rhs) <= epsilon;
}

bool nearMat4(const glm::mat4& lhs, const glm::mat4& rhs, float epsilon = EPSILON)
{
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            if (std::fabs(lhs[col][row] - rhs[col][row]) > epsilon) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

TEST(SkeletonAnimationSamplerTest, WrapTimeHandlesPositiveNegativeAndZeroDuration)
{
    EXPECT_DOUBLE_EQ(ya::SkeletonAnimationSampler::wrapTime(12.0, 5.0), 2.0);
    EXPECT_DOUBLE_EQ(ya::SkeletonAnimationSampler::wrapTime(-1.0, 5.0), 4.0);
    EXPECT_DOUBLE_EQ(ya::SkeletonAnimationSampler::wrapTime(3.0, 0.0), 0.0);
}

TEST(SkeletonAnimationSamplerTest, SampleVectorTrackHandlesFallbackSingleKeyLerpAndClamp)
{
    const glm::vec3 fallback(3.0f, 4.0f, 5.0f);
    EXPECT_TRUE(nearVec3(ya::SkeletonAnimationSampler::sampleVectorTrack({}, 1.0, fallback), fallback));

    std::vector<ya::SkeletonVectorKeyframe> keys = {
        {.time = 2.0, .value = glm::vec3(2.0f, 0.0f, 0.0f)},
    };
    EXPECT_TRUE(nearVec3(ya::SkeletonAnimationSampler::sampleVectorTrack(keys, 5.0, fallback), glm::vec3(2.0f, 0.0f, 0.0f)));

    keys.push_back(ya::SkeletonVectorKeyframe{.time = 6.0, .value = glm::vec3(10.0f, 0.0f, 0.0f)});
    EXPECT_TRUE(nearVec3(ya::SkeletonAnimationSampler::sampleVectorTrack(keys, 4.0, fallback), glm::vec3(6.0f, 0.0f, 0.0f)));
    EXPECT_TRUE(nearVec3(ya::SkeletonAnimationSampler::sampleVectorTrack(keys, 0.0, fallback), glm::vec3(2.0f, 0.0f, 0.0f)));
    EXPECT_TRUE(nearVec3(ya::SkeletonAnimationSampler::sampleVectorTrack(keys, 9.0, fallback), glm::vec3(10.0f, 0.0f, 0.0f)));
}

TEST(SkeletonAnimationSamplerTest, SampleQuatTrackSlerpsAndNormalizes)
{
    const glm::quat q90  = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::quat q180 = glm::angleAxis(glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f));

    std::vector<ya::SkeletonQuatKeyframe> keys = {
        {.time = 0.0, .value = q90},
        {.time = 10.0, .value = q180},
    };

    const glm::quat sampled = ya::SkeletonAnimationSampler::sampleQuatTrack(keys, 5.0);
    const glm::vec3 rotated = sampled * glm::vec3(1.0f, 0.0f, 0.0f);
    const float     angle   = std::atan2(rotated.y, rotated.x);

    EXPECT_NEAR(angle, glm::radians(135.0f), EPSILON);
    EXPECT_NEAR(glm::length(sampled), 1.0f, EPSILON);
}

TEST(SkeletonAnimationSamplerTest, SampleChannelPreservesBindPoseForMissingTracks)
{
    ya::SkeletonNodeInfo bindNode;
    bindNode.localTransform = glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, 0.0f, 0.0f)) *
                              glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 2.0f, 2.0f));

    ya::SkeletonAnimationChannel channel;
    channel.rotationKeys = {
        {.time = 0.0, .value = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f))},
    };

    const ya::SkeletonChannelSample sample = ya::SkeletonAnimationSampler::sampleChannel(channel, 0.0, &bindNode);
    EXPECT_TRUE(nearVec3(sample.position, glm::vec3(3.0f, 0.0f, 0.0f)));
    EXPECT_TRUE(nearVec3(sample.scale, glm::vec3(2.0f, 2.0f, 2.0f)));
}

TEST(SkeletonAnimationSamplerTest, SamplePoseComputesLocalGlobalAndBoneMatrices)
{
    ya::Skeleton skeleton;
    skeleton.nodes = {
        ya::SkeletonNodeInfo{
            .name           = "root",
            .parentIndex    = ya::INVALID_SKELETON_NODE_INDEX,
            .localTransform = glm::mat4(1.0f),
        },
        ya::SkeletonNodeInfo{
            .name           = "child",
            .parentIndex    = 0,
            .localTransform = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
        },
    };
    skeleton.bones = {
        ya::SkeletonBoneInfo{
            .name         = "child",
            .id           = 0,
            .nodeIndex    = 1,
            .offsetMatrix = glm::mat4(1.0f),
        },
    };

    ya::SkeletonAnimationClip clip;
    clip.duration = 10.0;
    clip.channels.push_back(ya::SkeletonAnimationChannel{
        .targetName   = "child",
        .nodeIndex    = 1,
        .boneIndex    = 0,
        .rotationKeys = {
            ya::SkeletonQuatKeyframe{.time = 0.0, .value = glm::quat(1.0f, 0.0f, 0.0f, 0.0f)},
            ya::SkeletonQuatKeyframe{.time = 10.0, .value = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f))},
        },
    });

    const ya::SkeletonPose pose = ya::SkeletonAnimationSampler::samplePose(skeleton, clip, 5.0, false);
    ASSERT_EQ(pose.localTransforms.size(), 2);
    ASSERT_EQ(pose.globalTransforms.size(), 2);
    ASSERT_EQ(pose.boneMatrices.size(), 1);

    const glm::mat4 expectedChildLocal = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f)) *
                                         glm::mat4_cast(glm::angleAxis(glm::radians(45.0f), glm::vec3(0.0f, 0.0f, 1.0f)));

    EXPECT_TRUE(nearMat4(pose.localTransforms[1], expectedChildLocal));
    EXPECT_TRUE(nearMat4(pose.globalTransforms[1], expectedChildLocal));
    EXPECT_TRUE(nearMat4(pose.boneMatrices[0], expectedChildLocal));
}
