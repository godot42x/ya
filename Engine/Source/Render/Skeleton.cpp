#include "Skeleton.h"

#include "Render/Model/ImportedModelData.h"

namespace ya
{
namespace
{
uint32_t findParentBoneIndex(const Skeleton&                               skeleton,
                             const std::unordered_map<uint32_t, uint32_t>& nodeToBoneIndex,
                             uint32_t                                      nodeIndex)
{
    if (nodeIndex >= skeleton.nodes.size()) {
        return INVALID_SKELETON_BONE_INDEX;
    }

    uint32_t parentNodeIndex = skeleton.nodes[nodeIndex].parentIndex;
    while (parentNodeIndex != INVALID_SKELETON_NODE_INDEX) {
        if (const auto boneIt = nodeToBoneIndex.find(parentNodeIndex); boneIt != nodeToBoneIndex.end()) {
            return boneIt->second;
        }

        if (parentNodeIndex >= skeleton.nodes.size()) {
            break;
        }
        parentNodeIndex = skeleton.nodes[parentNodeIndex].parentIndex;
    }

    return INVALID_SKELETON_BONE_INDEX;
}
} // namespace

std::shared_ptr<Skeleton> createSkeleton(const ImportedSkeletonData& importedSkeletonData)
{
    if (importedSkeletonData.isEmpty()) {
        return nullptr;
    }

    auto skeleton           = makeShared<Skeleton>();
    skeleton->name          = importedSkeletonData.name;
    skeleton->rootNodeIndex = importedSkeletonData.rootNodeIndex;
    skeleton->animations.reserve(importedSkeletonData.animations.size());

    skeleton->nodes.reserve(importedSkeletonData.nodes.size());
    for (size_t nodeIndex = 0; nodeIndex < importedSkeletonData.nodes.size(); ++nodeIndex) {
        const ImportedSkeletonNode& importedNode     = importedSkeletonData.nodes[nodeIndex];
        skeleton->nameToNodeIndex[importedNode.name] = static_cast<uint32_t>(nodeIndex);
        skeleton->nodes.push_back(SkeletonNodeInfo{
            .name            = importedNode.name,
            .parentIndex     = importedNode.parentIndex,
            .localTransform  = importedNode.localTransform,
            .globalTransform = importedNode.globalTransform,
        });
    }

    skeleton->bones.reserve(importedSkeletonData.bones.size());
    std::unordered_map<uint32_t, uint32_t> nodeToBoneIndex;
    for (const ImportedSkeletonBoneInfo& importedBone : importedSkeletonData.bones) {
        const uint32_t boneIndex = static_cast<uint32_t>(skeleton->bones.size());

        SkeletonBoneInfo runtimeBone;
        runtimeBone.name              = importedBone.name;
        runtimeBone.id                = importedBone.boneIndex;
        runtimeBone.nodeIndex         = importedBone.nodeIndex;
        runtimeBone.offsetMatrix      = importedBone.offsetMatrix;
        runtimeBone.inverseBindMatrix = importedBone.offsetMatrix;

        if (importedBone.nodeIndex < skeleton->nodes.size()) {
            runtimeBone.localBindTransform          = skeleton->nodes[importedBone.nodeIndex].localTransform;
            runtimeBone.globalBindTransform         = skeleton->nodes[importedBone.nodeIndex].globalTransform;
            nodeToBoneIndex[importedBone.nodeIndex] = boneIndex;
        }

        skeleton->nameToBoneIndex[runtimeBone.name] = boneIndex;
        skeleton->bones.push_back(runtimeBone);
    }

    for (uint32_t boneIndex = 0; boneIndex < skeleton->bones.size(); ++boneIndex) {
        SkeletonBoneInfo& runtimeBone = skeleton->bones[boneIndex];
        runtimeBone.parentIndex       = findParentBoneIndex(*skeleton, nodeToBoneIndex, runtimeBone.nodeIndex);
        if (runtimeBone.parentIndex == INVALID_SKELETON_BONE_INDEX && skeleton->rootBoneIndex == INVALID_SKELETON_BONE_INDEX) {
            skeleton->rootBoneIndex = boneIndex;
        }
    }

    for (const ImportedSkeletonAnimationClip& importedClip : importedSkeletonData.animations) {
        SkeletonAnimationClip clip;
        clip.name           = importedClip.name;
        clip.duration       = importedClip.duration;
        clip.ticksPerSecond = importedClip.ticksPerSecond;
        clip.channels.reserve(importedClip.channels.size());

        for (const ImportedSkeletonAnimationChannel& importedChannel : importedClip.channels) {
            SkeletonAnimationChannel channel;
            channel.targetName = importedChannel.targetName;

            if (const auto nodeIt = skeleton->nameToNodeIndex.find(channel.targetName); nodeIt != skeleton->nameToNodeIndex.end()) {
                channel.nodeIndex = nodeIt->second;
            }
            if (const auto boneIt = skeleton->nameToBoneIndex.find(channel.targetName); boneIt != skeleton->nameToBoneIndex.end()) {
                channel.boneIndex = boneIt->second;
            }

            channel.positionKeys.reserve(importedChannel.positionKeys.size());
            for (const ImportedSkeletonVectorKeyframe& importedKey : importedChannel.positionKeys) {
                channel.positionKeys.push_back(SkeletonVectorKeyframe{
                    .time  = importedKey.time,
                    .value = importedKey.value,
                });
            }

            channel.rotationKeys.reserve(importedChannel.rotationKeys.size());
            for (const ImportedSkeletonQuatKeyframe& importedKey : importedChannel.rotationKeys) {
                channel.rotationKeys.push_back(SkeletonQuatKeyframe{
                    .time  = importedKey.time,
                    .value = importedKey.value,
                });
            }

            channel.scaleKeys.reserve(importedChannel.scaleKeys.size());
            for (const ImportedSkeletonVectorKeyframe& importedKey : importedChannel.scaleKeys) {
                channel.scaleKeys.push_back(SkeletonVectorKeyframe{
                    .time  = importedKey.time,
                    .value = importedKey.value,
                });
            }

            clip.channels.push_back(std::move(channel));
        }

        skeleton->animations.push_back(std::move(clip));
    }

    return skeleton;
}

} // namespace ya
