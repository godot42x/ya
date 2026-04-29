#pragma once

#include "Core/Base.h"
#include "Render/Model/ImportedAnimationData.h"

#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace ya
{

struct ImportedSkeletonNode
{
    FName                 name;
    uint32_t              parentIndex = std::numeric_limits<uint32_t>::max();
    glm::mat4             localTransform{1.0f};
    glm::mat4             globalTransform{1.0f};
    std::vector<uint32_t> children;
};

struct ImportedSkeletonBoneInfo
{
    FName     name;
    glm::mat4 offsetMatrix{1.0f};
    uint32_t  boneIndex = 0;
    uint32_t  nodeIndex = std::numeric_limits<uint32_t>::max();
};

struct ImportedSkeletonData
{
    std::string                         name;
    std::unordered_map<FName, uint32_t> nameToNodeIndex;
    std::vector<ImportedSkeletonNode>   nodes;
    uint32_t                            rootNodeIndex = std::numeric_limits<uint32_t>::max();

    std::vector<ImportedSkeletonBoneInfo>      bones;
    std::unordered_map<FName, uint32_t>        boneNameToIndex;
    std::vector<ImportedSkeletonAnimationClip> animations;

    std::vector<uint32_t> meshIndices;

    bool isEmpty() const
    {
        return nodes.empty() && bones.empty() && animations.empty();
    }
};

} // namespace ya
