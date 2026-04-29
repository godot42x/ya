#pragma once

#include "Core/FName.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ya
{

struct ImportedSkeletonData;

inline constexpr uint32_t INVALID_SKELETON_NODE_INDEX = std::numeric_limits<uint32_t>::max();
inline constexpr uint32_t INVALID_SKELETON_BONE_INDEX = std::numeric_limits<uint32_t>::max();

struct SkeletonNodeInfo
{
    FName     name;
    uint32_t  parentIndex = INVALID_SKELETON_NODE_INDEX;
    glm::mat4 localTransform{1.0f};
    glm::mat4 globalTransform{1.0f};
};

struct SkeletonBoneInfo
{
    FName     name;
    uint32_t  id          = 0;
    uint32_t  nodeIndex   = INVALID_SKELETON_NODE_INDEX;
    uint32_t  parentIndex = INVALID_SKELETON_BONE_INDEX;
    glm::mat4 localBindTransform{1.0f};
    glm::mat4 globalBindTransform{1.0f};
    glm::mat4 offsetMatrix{1.0f};
    glm::mat4 inverseBindMatrix{1.0f};
};

struct SkeletonVectorKeyframe
{
    double    time = 0.0;
    glm::vec3 value{0.0f};
};

struct SkeletonQuatKeyframe
{
    double    time = 0.0;
    glm::quat value{1.0f, 0.0f, 0.0f, 0.0f};
};

struct SkeletonAnimationChannel
{
    FName    targetName;
    uint32_t nodeIndex = INVALID_SKELETON_NODE_INDEX;
    uint32_t boneIndex = INVALID_SKELETON_BONE_INDEX;

    std::vector<SkeletonVectorKeyframe> positionKeys;
    std::vector<SkeletonQuatKeyframe>   rotationKeys;
    std::vector<SkeletonVectorKeyframe> scaleKeys;
};

struct SkeletonAnimationClip
{
    std::string                           name;
    double                                duration       = 0.0;
    double                                ticksPerSecond = 0.0;
    std::vector<SkeletonAnimationChannel> channels;
};

struct Skeleton
{
    std::string name;

    std::vector<SkeletonNodeInfo>      nodes;
    std::vector<SkeletonBoneInfo>      bones;
    std::vector<SkeletonAnimationClip> animations;

    uint32_t rootNodeIndex = INVALID_SKELETON_NODE_INDEX;
    uint32_t rootBoneIndex = INVALID_SKELETON_BONE_INDEX;

    std::unordered_map<FName, uint32_t> nameToNodeIndex;
    std::unordered_map<FName, uint32_t> nameToBoneIndex;

    bool hasNodeHierarchy() const { return !nodes.empty(); }
    bool hasBones() const { return !bones.empty(); }
    bool hasAnimations() const { return !animations.empty(); }

    const SkeletonNodeInfo* getNode(size_t index) const
    {
        return index < nodes.size() ? &nodes[index] : nullptr;
    }

    const SkeletonNodeInfo* findNode(const FName& nodeName) const
    {
        auto it = nameToNodeIndex.find(nodeName);
        return it != nameToNodeIndex.end() && it->second < nodes.size() ? &nodes[it->second] : nullptr;
    }

    const SkeletonBoneInfo* getBone(size_t index) const
    {
        return index < bones.size() ? &bones[index] : nullptr;
    }

    const SkeletonBoneInfo* findBone(const FName& boneName) const
    {
        auto it = nameToBoneIndex.find(boneName);
        return it != nameToBoneIndex.end() && it->second < bones.size() ? &bones[it->second] : nullptr;
    }

    bool hasBone(const FName& name) const
    {
        return findBone(name) != nullptr;
    }

    const SkeletonAnimationClip* getAnimation(size_t index) const
    {
        return index < animations.size() ? &animations[index] : nullptr;
    }
};

std::shared_ptr<Skeleton> createSkeleton(const ImportedSkeletonData& importedSkeletonData);

} // namespace ya
