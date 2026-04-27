#include "Model.h"

#include "Render/EngineGeometryNormalizer.h"
#include "Render/Model/ImportedModelData.h"
#include "Render/Model/ModelImporterRegistry.h"

#include "Core/Log.h"

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

ImportedModelData ImportedModelData::decode(const std::string& filepath)
{
    return model_importer::getImporterForPath(filepath).import(filepath);
}

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

            clip.channels.push_back(channel);
        }

        skeleton->animations.push_back(std::move(clip));
    }

    return skeleton;
}

std::shared_ptr<Model> ImportedModelData::createModel() const
{
    auto model      = makeShared<Model>();
    model->filepath = filepath;
    model->setDirectory(directory);

    for (size_t meshIndex = 0; meshIndex < meshes.size(); ++meshIndex) {
        auto engineMeshData = buildEngineMeshData(*this, meshIndex);
        auto mesh           = Mesh::create(engineMeshData);
        model->meshes.push_back(std::move(mesh));
    }

    model->embeddedMaterials   = materials;
    model->meshMaterialIndices = meshMaterialIndices;
    model->meshSkeletonIndices = meshSkeletonIndices;

    model->skeletons.reserve(skeletons.size());
    for (const ImportedSkeletonData& importedSkeleton : skeletons) {
        model->skeletons.push_back(createSkeleton(importedSkeleton));
    }

    model->setIsLoaded(true);

    YA_CORE_INFO("ImportedModelData::createModel: '{}' -> {} GPU meshes, {} skeletons",
                 filepath,
                 model->meshes.size(),
                 model->skeletons.size());

    return model;
}

} // namespace ya
